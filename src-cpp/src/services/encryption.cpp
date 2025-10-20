#include "queen/encryption.hpp"
#include <spdlog/spdlog.h>
#include <openssl/evp.h>
#include <openssl/rand.h>
#include <openssl/err.h>
#include <cstring>
#include <stdexcept>
#include <sstream>
#include <iomanip>

namespace queen {

// Global instance
EncryptionService* g_encryption_service = nullptr;

// Base64 encoding/decoding
static const std::string base64_chars = 
    "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
    "abcdefghijklmnopqrstuvwxyz"
    "0123456789+/";

std::string EncryptionService::base64_encode(const uint8_t* data, size_t len) {
    std::string ret;
    int i = 0;
    uint8_t char_array_3[3];
    uint8_t char_array_4[4];
    
    while (len--) {
        char_array_3[i++] = *(data++);
        if (i == 3) {
            char_array_4[0] = (char_array_3[0] & 0xfc) >> 2;
            char_array_4[1] = ((char_array_3[0] & 0x03) << 4) + ((char_array_3[1] & 0xf0) >> 4);
            char_array_4[2] = ((char_array_3[1] & 0x0f) << 2) + ((char_array_3[2] & 0xc0) >> 6);
            char_array_4[3] = char_array_3[2] & 0x3f;
            
            for (i = 0; i < 4; i++)
                ret += base64_chars[char_array_4[i]];
            i = 0;
        }
    }
    
    if (i) {
        for (int j = i; j < 3; j++)
            char_array_3[j] = '\0';
            
        char_array_4[0] = (char_array_3[0] & 0xfc) >> 2;
        char_array_4[1] = ((char_array_3[0] & 0x03) << 4) + ((char_array_3[1] & 0xf0) >> 4);
        char_array_4[2] = ((char_array_3[1] & 0x0f) << 2) + ((char_array_3[2] & 0xc0) >> 6);
        
        for (int j = 0; j < i + 1; j++)
            ret += base64_chars[char_array_4[j]];
            
        while (i++ < 3)
            ret += '=';
    }
    
    return ret;
}

std::vector<uint8_t> EncryptionService::base64_decode(const std::string& encoded) {
    size_t in_len = encoded.size();
    int i = 0;
    int in_ = 0;
    uint8_t char_array_4[4], char_array_3[3];
    std::vector<uint8_t> ret;
    
    while (in_len-- && (encoded[in_] != '=') && 
           (isalnum(encoded[in_]) || (encoded[in_] == '+') || (encoded[in_] == '/'))) {
        char_array_4[i++] = encoded[in_]; in_++;
        if (i == 4) {
            for (i = 0; i < 4; i++)
                char_array_4[i] = base64_chars.find(char_array_4[i]);
                
            char_array_3[0] = (char_array_4[0] << 2) + ((char_array_4[1] & 0x30) >> 4);
            char_array_3[1] = ((char_array_4[1] & 0xf) << 4) + ((char_array_4[2] & 0x3c) >> 2);
            char_array_3[2] = ((char_array_4[2] & 0x3) << 6) + char_array_4[3];
            
            for (i = 0; i < 3; i++)
                ret.push_back(char_array_3[i]);
            i = 0;
        }
    }
    
    if (i) {
        for (int j = 0; j < i; j++)
            char_array_4[j] = base64_chars.find(char_array_4[j]);
            
        char_array_3[0] = (char_array_4[0] << 2) + ((char_array_4[1] & 0x30) >> 4);
        char_array_3[1] = ((char_array_4[1] & 0xf) << 4) + ((char_array_4[2] & 0x3c) >> 2);
        
        for (int j = 0; j < i - 1; j++)
            ret.push_back(char_array_3[j]);
    }
    
    return ret;
}

EncryptionService::EncryptionService() : enabled_(false) {
    // Read encryption key from environment
    const char* key_hex = std::getenv("QUEEN_ENCRYPTION_KEY");
    
    if (!key_hex || std::strlen(key_hex) == 0) {
        spdlog::warn("Encryption disabled - QUEEN_ENCRYPTION_KEY not set");
        return;
    }
    
    // Parse hex key
    std::string key_str(key_hex);
    if (key_str.length() != 64) {  // 32 bytes = 64 hex chars
        spdlog::error("QUEEN_ENCRYPTION_KEY must be 32 bytes (64 hex characters), got {}", key_str.length());
        return;
    }
    
    // Convert hex to bytes
    key_.reserve(KEY_SIZE);
    for (size_t i = 0; i < key_str.length(); i += 2) {
        std::string byte_str = key_str.substr(i, 2);
        uint8_t byte = static_cast<uint8_t>(std::stoi(byte_str, nullptr, 16));
        key_.push_back(byte);
    }
    
    if (key_.size() != KEY_SIZE) {
        spdlog::error("Failed to parse encryption key correctly");
        key_.clear();
        return;
    }
    
    enabled_ = true;
    spdlog::info("Encryption service initialized (AES-256-GCM)");
}

std::optional<EncryptionService::EncryptedData> EncryptionService::encrypt_payload(const std::string& payload_json) {
    if (!enabled_) {
        return std::nullopt;
    }
    
    try {
        // Generate random IV
        uint8_t iv[IV_SIZE];
        if (RAND_bytes(iv, IV_SIZE) != 1) {
            spdlog::error("Failed to generate IV");
            return std::nullopt;
        }
        
        // Create cipher context
        EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
        if (!ctx) {
            spdlog::error("Failed to create cipher context");
            return std::nullopt;
        }
        
        // Initialize encryption with AES-256-GCM
        if (EVP_EncryptInit_ex(ctx, EVP_aes_256_gcm(), nullptr, key_.data(), iv) != 1) {
            EVP_CIPHER_CTX_free(ctx);
            spdlog::error("Failed to initialize encryption");
            return std::nullopt;
        }
        
        // Encrypt the payload
        std::vector<uint8_t> ciphertext;
        ciphertext.resize(payload_json.length() + EVP_CIPHER_block_size(EVP_aes_256_gcm()));
        
        int len = 0;
        if (EVP_EncryptUpdate(ctx, ciphertext.data(), &len, 
                            reinterpret_cast<const uint8_t*>(payload_json.data()), 
                            payload_json.length()) != 1) {
            EVP_CIPHER_CTX_free(ctx);
            spdlog::error("Failed to encrypt data");
            return std::nullopt;
        }
        
        int ciphertext_len = len;
        
        // Finalize encryption
        if (EVP_EncryptFinal_ex(ctx, ciphertext.data() + len, &len) != 1) {
            EVP_CIPHER_CTX_free(ctx);
            spdlog::error("Failed to finalize encryption");
            return std::nullopt;
        }
        
        ciphertext_len += len;
        ciphertext.resize(ciphertext_len);
        
        // Get authentication tag
        uint8_t tag[TAG_SIZE];
        if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_GET_TAG, TAG_SIZE, tag) != 1) {
            EVP_CIPHER_CTX_free(ctx);
            spdlog::error("Failed to get authentication tag");
            return std::nullopt;
        }
        
        EVP_CIPHER_CTX_free(ctx);
        
        // Return encrypted data
        EncryptedData result;
        result.encrypted = base64_encode(ciphertext.data(), ciphertext.size());
        result.iv = base64_encode(iv, IV_SIZE);
        result.auth_tag = base64_encode(tag, TAG_SIZE);
        
        return result;
        
    } catch (const std::exception& e) {
        spdlog::error("Encryption failed: {}", e.what());
        return std::nullopt;
    }
}

std::optional<std::string> EncryptionService::decrypt_payload(const EncryptedData& encrypted) {
    if (!enabled_) {
        return std::nullopt;
    }
    
    try {
        // Decode base64 data
        auto ciphertext = base64_decode(encrypted.encrypted);
        auto iv = base64_decode(encrypted.iv);
        auto tag = base64_decode(encrypted.auth_tag);
        
        if (iv.size() != IV_SIZE || tag.size() != TAG_SIZE) {
            spdlog::error("Invalid IV or tag size");
            return std::nullopt;
        }
        
        // Create decipher context
        EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
        if (!ctx) {
            spdlog::error("Failed to create decipher context");
            return std::nullopt;
        }
        
        // Initialize decryption
        if (EVP_DecryptInit_ex(ctx, EVP_aes_256_gcm(), nullptr, key_.data(), iv.data()) != 1) {
            EVP_CIPHER_CTX_free(ctx);
            spdlog::error("Failed to initialize decryption");
            return std::nullopt;
        }
        
        // Decrypt the data
        std::vector<uint8_t> plaintext;
        plaintext.resize(ciphertext.size());
        
        int len = 0;
        if (EVP_DecryptUpdate(ctx, plaintext.data(), &len, ciphertext.data(), ciphertext.size()) != 1) {
            EVP_CIPHER_CTX_free(ctx);
            spdlog::error("Failed to decrypt data");
            return std::nullopt;
        }
        
        int plaintext_len = len;
        
        // Set authentication tag
        if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_TAG, TAG_SIZE, const_cast<uint8_t*>(tag.data())) != 1) {
            EVP_CIPHER_CTX_free(ctx);
            spdlog::error("Failed to set authentication tag");
            return std::nullopt;
        }
        
        // Finalize decryption (this verifies the tag)
        if (EVP_DecryptFinal_ex(ctx, plaintext.data() + len, &len) != 1) {
            EVP_CIPHER_CTX_free(ctx);
            spdlog::error("Decryption failed - authentication tag verification failed");
            return std::nullopt;
        }
        
        plaintext_len += len;
        plaintext.resize(plaintext_len);
        
        EVP_CIPHER_CTX_free(ctx);
        
        // Return decrypted JSON string
        return std::string(reinterpret_cast<char*>(plaintext.data()), plaintext.size());
        
    } catch (const std::exception& e) {
        spdlog::error("Decryption failed: {}", e.what());
        return std::nullopt;
    }
}

// Global initialization
bool init_encryption() {
    if (g_encryption_service) {
        delete g_encryption_service;
    }
    
    g_encryption_service = new EncryptionService();
    return g_encryption_service->is_enabled();
}

EncryptionService* get_encryption_service() {
    return g_encryption_service;
}

} // namespace queen
