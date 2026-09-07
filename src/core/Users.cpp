#include "Users.h"

#include <bearssl/bearssl_hmac.h>
#include <bearssl/bearssl_hash.h>
#include <cstring>

namespace {
    void GenerateSalt(uint8_t* Salt, size_t Len) {
        for(size_t i = 0; i < Len; i += 4) {
            uint32_t Random = RANDOM_REG32;
            size_t n = (Len - i < 4) ? (Len - i) : 4;
            memcpy(Salt + i, &Random, n);
        }
    }

    bool ConstantTimeEqual(const uint8_t* A, const uint8_t* B, size_t Len) {
        uint8_t Diff = 0;
        for(size_t i = 0; i < Len; i++) Diff |= A[i] ^ B[i];
        return Diff == 0;
    }
}

// Single-block PBKDF2 (RFC 8018): since OutLen never exceeds the SHA-256
// output size here, DK is just F(P, S, c, 1) - no need to derive and
// concatenate further blocks.
void PBKDF2_HMAC_SHA256(const uint8_t* Password, size_t PasswordLen, const uint8_t* Salt, size_t SaltLen, uint32_t Iterations, uint8_t* Out, size_t OutLen) {
    br_hmac_key_context KeyContext;
    br_hmac_key_init(&KeyContext, &br_sha256_vtable, Password, PasswordLen);

    uint8_t U[PASS_HASHLEN];
    br_hmac_context Hmac;
    br_hmac_init(&Hmac, &KeyContext, 0);
    br_hmac_update(&Hmac, Salt, SaltLen);
    const uint8_t BlockIndex[4] = { 0, 0, 0, 1 };
    br_hmac_update(&Hmac, BlockIndex, sizeof(BlockIndex));
    br_hmac_out(&Hmac, U);

    uint8_t T[PASS_HASHLEN];
    memcpy(T, U, PASS_HASHLEN);

    for(uint32_t i = 1; i < Iterations; i++) {
        br_hmac_init(&Hmac, &KeyContext, 0);
        br_hmac_update(&Hmac, U, PASS_HASHLEN);
        br_hmac_out(&Hmac, U);
        for(uint8_t b = 0; b < PASS_HASHLEN; b++) T[b] ^= U[b];
    }

    memcpy(Out, T, OutLen);
}

void user::SetPassword(const String& Password) {
    GenerateSalt(pSalt, PASS_SALTLEN);
    PBKDF2_HMAC_SHA256((const uint8_t*)Password.c_str(), Password.length(), pSalt, PASS_SALTLEN, PASS_PBKDF2_ITERATIONS, pHash, PASS_HASHLEN);
}

bool user::Authenticate(const String& Password) const {
    uint8_t ComputedHash[PASS_HASHLEN];
    PBKDF2_HMAC_SHA256((const uint8_t*)Password.c_str(), Password.length(), pSalt, PASS_SALTLEN, PASS_PBKDF2_ITERATIONS, ComputedHash, PASS_HASHLEN);
    return ConstantTimeEqual(ComputedHash, pHash, PASS_HASHLEN);
}

bool users::NormalizeUsername(String& Username) {
    Username.trim();
    Username.toLowerCase();

    if(Username.length() < USERNAME_MIN_LENGTH || Username.length() > USERNAME_MAX_LENGTH) return false;

    for(size_t i = 0; i < Username.length(); i++) {
        char c = Username.charAt(i);
        bool valid = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '.' || c == '_' || c == '-';
        if(!valid) return false;
    }

    return true;
}

int users::FindIndex(const String& Username) const {
    for(size_t i = 0; i < mCount; i++) if(mUsers[i].Username() == Username) return (int)i;
    return -1;
}

size_t users::CountAdmins() const {
    size_t Count = 0;
    for(size_t i = 0; i < mCount; i++) if(mUsers[i].Admin()) Count++;
    return Count;
}

UserReturn users::Add(String Username, const String& Password, bool Admin) {
    if(!NormalizeUsername(Username)) return UserReturn::InvalidUsername;
    if(Password.length() < PASSWORD_MIN_LENGTH || Password.length() > PASSWORD_MAX_LENGTH) return UserReturn::InvalidPassword;
    if(FindIndex(Username) >= 0) return UserReturn::UserExists;
    if(mCount >= MAX_USERS) return UserReturn::MaxUsersReached;

    if(mCount == 0) Admin = true; // The very first account must be an admin - mirrors DeviceIQ.

    user& NewUser = mUsers[mCount];
    NewUser.Username(Username);
    NewUser.Admin(Admin);
    NewUser.SetPassword(Password);
    mCount++;

    return UserReturn::NoError;
}

UserReturn users::AddStored(String Username, bool Admin, const uint8_t (&Salt)[PASS_SALTLEN], const uint8_t (&Hash)[PASS_HASHLEN]) {
    if(!NormalizeUsername(Username)) return UserReturn::InvalidUsername;
    if(FindIndex(Username) >= 0) return UserReturn::UserExists;
    if(mCount >= MAX_USERS) return UserReturn::MaxUsersReached;

    user& NewUser = mUsers[mCount];
    NewUser.Username(Username);
    NewUser.Admin(Admin);
    NewUser.SetStoredCredentials(Salt, Hash);
    mCount++;

    return UserReturn::NoError;
}

UserReturn users::Remove(String Username) {
    if(!NormalizeUsername(Username)) return UserReturn::InvalidUsername;
    int Index = FindIndex(Username);
    if(Index < 0) return UserReturn::UserNotFound;
    if(mUsers[Index].Admin() && CountAdmins() <= 1) return UserReturn::NoAdminRemaining;

    for(size_t i = Index; i < mCount - 1; i++) mUsers[i] = mUsers[i + 1];
    mCount--;

    return UserReturn::NoError;
}

UserReturn users::Rename(String CurrentUsername, String NewUsername) {
    if(!NormalizeUsername(CurrentUsername) || !NormalizeUsername(NewUsername)) return UserReturn::InvalidUsername;

    int Index = FindIndex(CurrentUsername);
    if(Index < 0) return UserReturn::UserNotFound;
    if(NewUsername == CurrentUsername) return UserReturn::NoError;
    if(FindIndex(NewUsername) >= 0) return UserReturn::UserExists;

    mUsers[Index].Username(NewUsername);
    return UserReturn::NoError;
}

UserReturn users::SetPassword(String Username, const String& NewPassword) {
    if(!NormalizeUsername(Username)) return UserReturn::InvalidUsername;
    if(NewPassword.length() < PASSWORD_MIN_LENGTH || NewPassword.length() > PASSWORD_MAX_LENGTH) return UserReturn::InvalidPassword;

    int Index = FindIndex(Username);
    if(Index < 0) return UserReturn::UserNotFound;

    mUsers[Index].SetPassword(NewPassword);
    return UserReturn::NoError;
}

UserReturn users::SetAdmin(String Username, bool Admin) {
    if(!NormalizeUsername(Username)) return UserReturn::InvalidUsername;
    int Index = FindIndex(Username);
    if(Index < 0) return UserReturn::UserNotFound;
    if(mUsers[Index].Admin() == Admin) return UserReturn::NoError;
    if(!Admin && mUsers[Index].Admin() && CountAdmins() <= 1) return UserReturn::NoAdminRemaining;

    mUsers[Index].Admin(Admin);
    return UserReturn::NoError;
}

bool users::Authenticate(String Username, const String& Password) const {
    if(!NormalizeUsername(Username)) return false;
    int Index = FindIndex(Username);
    if(Index < 0) return false;
    return mUsers[Index].Authenticate(Password);
}

bool users::Find(String Username, UserInfo* Out) const {
    if(!NormalizeUsername(Username)) return false;
    int Index = FindIndex(Username);
    if(Index < 0) return false;
    if(Out) { Out->Username = mUsers[Index].Username(); Out->Admin = mUsers[Index].Admin(); }
    return true;
}
