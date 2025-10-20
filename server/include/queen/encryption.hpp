#pragma once

#include <string>
#include <optional>
#include <vector>
#include <cstdint>

namespace queen {

class EncryptionService {
private:
    std::vector<uint8_t> key_;
    bool enabled_;
    
    static constexpr int KEY_SIZE = 32;  // 256 bits
    static constexpr int IV_SIZE = 16;   // 128 bits
    static constexpr int TAG_SIZE = 16;  // 128 bits
    
public:
    EncryptionService();
    
    bool is_enabled() const { return enabled_; }
    
    struct EncryptedData {
        std::string encrypted;  // Base64-encoded encrypted data
        std::string iv;         // Base64-encoded IV
        std::string auth_tag;   // Base64-encoded authentication tag
    };
    
    // Encrypt a JSON payload
    std::optional<EncryptedData> encrypt_payload(const std::string& payload_json);
    
    // Decrypt encrypted data back to JSON
    std::optional<std::string> decrypt_payload(const EncryptedData& encrypted);
    
    // Utility functions
    static std::string base64_encode(const uint8_t* data, size_t len);
    static std::vector<uint8_t> base64_decode(const std::string& encoded);
};

// Global encryption service instance
extern EncryptionService* g_encryption_service;

// Initialize encryption service from environment
bool init_encryption();

// Get global instance
EncryptionService* get_encryption_service();

} // namespace queen
