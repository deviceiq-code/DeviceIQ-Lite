#pragma once

#include <Arduino.h>

// DeviceIQ (the non-Lite sibling) hashes passwords with 100,000 PBKDF2
// iterations, tuned for an ESP32 verifying credentials once per login,
// behind a session cookie. Lite authenticates the same way (login once,
// session cookie afterwards) but on a much slower ESP8266, so a far smaller
// iteration count is used on purpose - still salted and hashed (never
// plaintext), just not tuned for the same threat model.
constexpr uint32_t PASS_PBKDF2_ITERATIONS = 1000;
constexpr uint8_t PASS_SALTLEN = 16;
constexpr uint8_t PASS_HASHLEN = 32; // SHA-256 output - keeps PBKDF2 to a single block.
constexpr uint8_t MAX_USERS = 3;
constexpr uint8_t USERNAME_MIN_LENGTH = 3;
constexpr uint8_t USERNAME_MAX_LENGTH = 32;
constexpr uint8_t PASSWORD_MIN_LENGTH = 8;
constexpr uint8_t PASSWORD_MAX_LENGTH = 64;

enum class UserReturn : uint8_t {
    NoError = 0,
    UserExists,
    UserNotFound,
    MaxUsersReached,
    NoAdminRemaining,
    InvalidUsername,
    InvalidPassword,
    InvalidCredentials
};

struct UserInfo {
    String Username;
    bool Admin = false;
};

class user {
    public:
        const String& Username() const { return pUsername; }
        bool Admin() const { return pAdmin; }

    private:
        friend class users;

        void Username(const String& Value) { pUsername = Value; }
        void Admin(bool Value) { pAdmin = Value; }
        void SetPassword(const String& Password);
        bool Authenticate(const String& Password) const;
        void SetStoredCredentials(const uint8_t (&Salt)[PASS_SALTLEN], const uint8_t (&Hash)[PASS_HASHLEN]) {
            memcpy(pSalt, Salt, PASS_SALTLEN);
            memcpy(pHash, Hash, PASS_HASHLEN);
        }
        void CopyCredentials(uint8_t (&Salt)[PASS_SALTLEN], uint8_t (&Hash)[PASS_HASHLEN]) const {
            memcpy(Salt, pSalt, PASS_SALTLEN);
            memcpy(Hash, pHash, PASS_HASHLEN);
        }

        String pUsername;
        bool pAdmin = false;
        uint8_t pSalt[PASS_SALTLEN] = {0};
        uint8_t pHash[PASS_HASHLEN] = {0};
};

class users {
    public:
        // Trims/lowercases in place regardless, then returns whether the
        // result is a valid username (3-32 characters: lowercase letters,
        // digits, '.', '_', '-') - matches DeviceIQ's user::NormalizeUsername.
        static bool NormalizeUsername(String& Username);

        size_t Count() const { return mCount; }
        size_t CountAdmins() const;

        UserReturn Add(String Username, const String& Password, bool Admin = false);
        // Restores a user from config.json's persisted Salt/Hash, bypassing
        // SetPassword()'s PBKDF2 computation - the credential was already
        // derived once, when the password was actually set.
        UserReturn AddStored(String Username, bool Admin, const uint8_t (&Salt)[PASS_SALTLEN], const uint8_t (&Hash)[PASS_HASHLEN]);
        UserReturn Remove(String Username);
        UserReturn Rename(String CurrentUsername, String NewUsername);
        UserReturn SetPassword(String Username, const String& NewPassword);
        UserReturn SetAdmin(String Username, bool Admin);
        bool Authenticate(String Username, const String& Password) const;
        bool Find(String Username, UserInfo* Out = nullptr) const;

        template<typename Visitor>
        void ForEachStored(Visitor&& visitor) const {
            for(size_t i = 0; i < mCount; i++) {
                const user& current = mUsers[i];
                visitor(current.Username(), current.Admin(), current.pSalt, current.pHash);
            }
        }

    private:
        int FindIndex(const String& Username) const;

        user mUsers[MAX_USERS];
        size_t mCount = 0;
};

// PBKDF2-HMAC-SHA256, restricted to a single block (OutLen <= 32) - the
// only case this project needs, since PASS_HASHLEN is the SHA-256 output
// size. Implemented on top of BearSSL, which ships with the ESP8266 core
// but - unlike mbedTLS on ESP32 - has no ready-made PBKDF2 of its own.
void PBKDF2_HMAC_SHA256(const uint8_t* Password, size_t PasswordLen, const uint8_t* Salt, size_t SaltLen, uint32_t Iterations, uint8_t* Out, size_t OutLen);
