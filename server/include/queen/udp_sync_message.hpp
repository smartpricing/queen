#pragma once

#include <json.hpp>
#include <string>
#include <vector>
#include <cstdint>
#include <cstring>
#include <chrono>
#include <random>
#include <array>
#include <atomic>
#include <openssl/hmac.h>
#include <openssl/evp.h>

namespace queen {

/**
 * UDP Sync Message Types
 * 
 * These message types are used for inter-instance cache synchronization.
 * The protocol uses MessagePack for compact binary encoding.
 */
enum class UDPSyncMessageType : uint8_t {
    // === Core notifications ===
    MESSAGE_AVAILABLE = 1,      // New message pushed
    PARTITION_FREE = 2,         // Partition acknowledged
    HEARTBEAT = 3,              // Server alive
    
    // === Queue Config (Tier 1) ===
    QUEUE_CONFIG_SET = 10,      // Queue created or updated
    QUEUE_CONFIG_DELETE = 11,   // Queue deleted
    
    // === Consumer Presence (Tier 2) ===
    CONSUMER_REGISTERED = 20,   // Server has consumer for queue
    CONSUMER_DEREGISTERED = 21, // Server no longer has consumer for queue
    
    // === Partition (Tier 3) ===
    PARTITION_DELETED = 25,     // Partition deleted by retention (invalidate cache)
    
    // === Lease Hints (Tier 4) ===
    LEASE_HINT_ACQUIRED = 30,   // Hint: server acquired lease
    LEASE_HINT_RELEASED = 31,   // Hint: server released lease
    
    // === System State (Tier 5) ===
    MAINTENANCE_MODE_SET = 40,  // Maintenance mode changed
};

/**
 * Convert message type to string for logging
 */
inline const char* message_type_to_string(UDPSyncMessageType type) {
    switch (type) {
        case UDPSyncMessageType::MESSAGE_AVAILABLE: return "MESSAGE_AVAILABLE";
        case UDPSyncMessageType::PARTITION_FREE: return "PARTITION_FREE";
        case UDPSyncMessageType::HEARTBEAT: return "HEARTBEAT";
        case UDPSyncMessageType::QUEUE_CONFIG_SET: return "QUEUE_CONFIG_SET";
        case UDPSyncMessageType::QUEUE_CONFIG_DELETE: return "QUEUE_CONFIG_DELETE";
        case UDPSyncMessageType::CONSUMER_REGISTERED: return "CONSUMER_REGISTERED";
        case UDPSyncMessageType::CONSUMER_DEREGISTERED: return "CONSUMER_DEREGISTERED";
        case UDPSyncMessageType::PARTITION_DELETED: return "PARTITION_DELETED";
        case UDPSyncMessageType::LEASE_HINT_ACQUIRED: return "LEASE_HINT_ACQUIRED";
        case UDPSyncMessageType::LEASE_HINT_RELEASED: return "LEASE_HINT_RELEASED";
        case UDPSyncMessageType::MAINTENANCE_MODE_SET: return "MAINTENANCE_MODE_SET";
        default: return "UNKNOWN";
    }
}

/**
 * Message header structure (fixed size: 92 bytes)
 * 
 * Layout:
 * - version: 1 byte
 * - type: 1 byte
 * - payload_length: 2 bytes (big-endian)
 * - signature: 32 bytes (HMAC-SHA256)
 * - sender_id: 32 bytes (null-terminated)
 * - session_id: 16 bytes (random UUID for restart detection)
 * - sequence: 8 bytes (big-endian, monotonic per session)
 */
constexpr size_t UDP_SYNC_HEADER_SIZE = 1 + 1 + 2 + 32 + 32 + 16 + 8;  // 92 bytes
constexpr size_t UDP_SYNC_MAX_PAYLOAD = 1308;  // Stay under 1400 MTU
constexpr size_t UDP_SYNC_MAX_MESSAGE = UDP_SYNC_HEADER_SIZE + UDP_SYNC_MAX_PAYLOAD;
constexpr uint8_t UDP_SYNC_VERSION = 1;

/**
 * Builder for constructing UDP sync messages
 */
class UDPSyncMessageBuilder {
public:
    UDPSyncMessageBuilder(const std::string& sender_id, const std::string& session_id)
        : sender_id_(sender_id), session_id_(session_id), sequence_(0) {
        // Truncate if too long
        if (sender_id_.length() > 31) sender_id_.resize(31);
        if (session_id_.length() > 15) session_id_.resize(15);
    }
    
    /**
     * Build a message with the given type and payload
     * Returns the serialized message buffer
     */
    std::vector<uint8_t> build(UDPSyncMessageType type, const nlohmann::json& payload,
                                const std::string& secret = "") {
        // Serialize payload to MessagePack
        std::vector<uint8_t> msgpack_payload = nlohmann::json::to_msgpack(payload);
        
        if (msgpack_payload.size() > UDP_SYNC_MAX_PAYLOAD) {
            throw std::runtime_error("Payload too large for UDP sync message");
        }
        
        // Allocate buffer
        std::vector<uint8_t> buffer(UDP_SYNC_HEADER_SIZE + msgpack_payload.size());
        
        // Version
        buffer[0] = UDP_SYNC_VERSION;
        
        // Type
        buffer[1] = static_cast<uint8_t>(type);
        
        // Payload length (big-endian)
        uint16_t len = static_cast<uint16_t>(msgpack_payload.size());
        buffer[2] = (len >> 8) & 0xFF;
        buffer[3] = len & 0xFF;
        
        // Signature placeholder (filled later)
        std::memset(&buffer[4], 0, 32);
        
        // Sender ID (null-terminated)
        std::memset(&buffer[36], 0, 32);
        std::memcpy(&buffer[36], sender_id_.c_str(), sender_id_.length());
        
        // Session ID
        std::memset(&buffer[68], 0, 16);
        std::memcpy(&buffer[68], session_id_.c_str(), session_id_.length());
        
        // Sequence (big-endian)
        uint64_t seq = ++sequence_;
        for (int i = 0; i < 8; i++) {
            buffer[84 + i] = (seq >> (56 - i * 8)) & 0xFF;
        }
        
        // Copy payload
        std::memcpy(&buffer[UDP_SYNC_HEADER_SIZE], msgpack_payload.data(), msgpack_payload.size());
        
        // Compute and set signature if secret provided
        if (!secret.empty()) {
            compute_signature(buffer, secret);
        }
        
        return buffer;
    }
    
    uint64_t current_sequence() const { return sequence_; }
    
private:
    std::string sender_id_;
    std::string session_id_;
    std::atomic<uint64_t> sequence_;
    
    void compute_signature(std::vector<uint8_t>& buffer, const std::string& secret) {
        // HMAC-SHA256 of everything except signature field
        // Sign: version + type + payload_length + sender_id + session_id + sequence + payload
        std::vector<uint8_t> to_sign;
        to_sign.reserve(buffer.size() - 32 + 4);  // Exclude signature, include header parts
        
        // version + type + length (4 bytes)
        to_sign.insert(to_sign.end(), buffer.begin(), buffer.begin() + 4);
        // sender_id + session_id + sequence + payload (skip signature at offset 4-36)
        to_sign.insert(to_sign.end(), buffer.begin() + 36, buffer.end());
        
        unsigned char signature[32];
        unsigned int sig_len = 32;
        
        HMAC(EVP_sha256(), 
             secret.c_str(), static_cast<int>(secret.length()),
             to_sign.data(), to_sign.size(),
             signature, &sig_len);
        
        std::memcpy(&buffer[4], signature, 32);
    }
};

/**
 * Parser for reading UDP sync messages
 */
class UDPSyncMessageParser {
public:
    UDPSyncMessageParser(const uint8_t* data, size_t size) 
        : data_(data), size_(size), valid_(false) {
        if (size >= UDP_SYNC_HEADER_SIZE) {
            valid_ = true;
        }
    }
    
    bool is_valid() const { return valid_; }
    
    uint8_t version() const {
        return valid_ ? data_[0] : 0;
    }
    
    UDPSyncMessageType type() const {
        return valid_ ? static_cast<UDPSyncMessageType>(data_[1]) : UDPSyncMessageType::HEARTBEAT;
    }
    
    uint16_t payload_length() const {
        if (!valid_) return 0;
        return (static_cast<uint16_t>(data_[2]) << 8) | data_[3];
    }
    
    std::array<uint8_t, 32> signature() const {
        std::array<uint8_t, 32> sig{};
        if (valid_) {
            std::memcpy(sig.data(), &data_[4], 32);
        }
        return sig;
    }
    
    std::string sender_id() const {
        if (!valid_) return "";
        char buf[33] = {0};
        std::memcpy(buf, &data_[36], 32);
        return std::string(buf);
    }
    
    std::string session_id() const {
        if (!valid_) return "";
        char buf[17] = {0};
        std::memcpy(buf, &data_[68], 16);
        return std::string(buf);
    }
    
    uint64_t sequence() const {
        if (!valid_) return 0;
        uint64_t seq = 0;
        for (int i = 0; i < 8; i++) {
            seq = (seq << 8) | data_[84 + i];
        }
        return seq;
    }
    
    /**
     * Get the payload as a span of bytes
     */
    std::pair<const uint8_t*, size_t> payload_bytes() const {
        if (!valid_) return {nullptr, 0};
        uint16_t len = payload_length();
        if (size_ < UDP_SYNC_HEADER_SIZE + len) {
            return {nullptr, 0};
        }
        return {&data_[UDP_SYNC_HEADER_SIZE], len};
    }
    
    /**
     * Parse payload as JSON (from MessagePack)
     */
    nlohmann::json payload() const {
        auto [ptr, len] = payload_bytes();
        if (!ptr || len == 0) return nlohmann::json{};
        try {
            return nlohmann::json::from_msgpack(ptr, ptr + len);
        } catch (...) {
            return nlohmann::json{};
        }
    }
    
    /**
     * Verify HMAC signature
     */
    bool verify_signature(const std::string& secret) const {
        if (!valid_ || secret.empty()) return false;
        
        // Reconstruct the data that was signed
        std::vector<uint8_t> to_sign;
        to_sign.reserve(size_ - 32 + 4);
        
        // version + type + length (4 bytes)
        to_sign.insert(to_sign.end(), data_, data_ + 4);
        // sender_id + session_id + sequence + payload (skip signature at offset 4-36)
        to_sign.insert(to_sign.end(), data_ + 36, data_ + size_);
        
        unsigned char expected[32];
        unsigned int sig_len = 32;
        
        HMAC(EVP_sha256(),
             secret.c_str(), static_cast<int>(secret.length()),
             to_sign.data(), to_sign.size(),
             expected, &sig_len);
        
        // Constant-time comparison
        const uint8_t* sig = &data_[4];
        int diff = 0;
        for (int i = 0; i < 32; i++) {
            diff |= sig[i] ^ expected[i];
        }
        return diff == 0;
    }
    
private:
    const uint8_t* data_;
    size_t size_;
    bool valid_;
};

/**
 * Generate a random session ID (16 characters hex)
 */
inline std::string generate_session_id() {
    static thread_local std::mt19937_64 rng(std::random_device{}());
    uint64_t val = rng();
    char buf[17];
    snprintf(buf, sizeof(buf), "%016llx", static_cast<unsigned long long>(val));
    return std::string(buf, 16);
}

/**
 * Get current timestamp in milliseconds
 */
inline int64_t now_ms() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()
    ).count();
}

} // namespace queen


