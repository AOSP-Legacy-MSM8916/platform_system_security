/*
 * Copyright (C) 2015 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#define LOG_TAG "KeystoreOperation"

#include "operation.h"

#include <algorithm>

namespace android {
OperationMap::OperationMap(IBinder::DeathRecipient* deathRecipient)
        : mDeathRecipient(deathRecipient) {
}

sp<IBinder> OperationMap::addOperation(keymaster_operation_handle_t handle,
                                       const keymaster1_device_t* dev,
                                       sp<IBinder> appToken,
                                       const keymaster_key_blob_t& key, bool pruneable) {
    sp<IBinder> token = new BBinder();
    mMap[token] = Operation(handle, dev, key, appToken);
    if (pruneable) {
        mLru.push_back(token);
    }
    if (mAppTokenMap.find(appToken) == mAppTokenMap.end()) {
        appToken->linkToDeath(mDeathRecipient);
    }
    mAppTokenMap[appToken].push_back(token);
    return token;
}

bool OperationMap::getOperation(sp<IBinder> token, keymaster_operation_handle_t* outHandle,
                                const keymaster1_device_t** outDevice,
                                keymaster_key_blob_t* key) {
    if (!outHandle || !outDevice) {
        return false;
    }
    auto entry = mMap.find(token);
    if (entry == mMap.end()) {
        return false;
    }
    updateLru(token);

    *outHandle = entry->second.handle;
    *outDevice = entry->second.device;
    if (key) {
        key->key_material_size = entry->second.key.key_material_size;
        uint8_t* material = new uint8_t[key->key_material_size];
        memcpy(reinterpret_cast<void*>(material), entry->second.key.key_material,
               key->key_material_size);
        key->key_material = material;
    }
    return true;
}

void OperationMap::updateLru(sp<IBinder> token) {
    auto lruEntry = std::find(mLru.begin(), mLru.end(), token);
    if (lruEntry != mLru.end()) {
        mLru.erase(lruEntry);
        mLru.push_back(token);
    }
}

bool OperationMap::removeOperation(sp<IBinder> token) {
    auto entry = mMap.find(token);
    if (entry == mMap.end()) {
        return false;
    }
    sp<IBinder> appToken = entry->second.appToken;
    delete[] entry->second.key.key_material;
    mMap.erase(entry);
    auto lruEntry = std::find(mLru.begin(), mLru.end(), token);
    if (lruEntry != mLru.end()) {
        mLru.erase(lruEntry);
    }
    removeOperationTracking(token, appToken);
    return true;
}

void OperationMap::removeOperationTracking(sp<IBinder> token, sp<IBinder> appToken) {
    auto appEntry = mAppTokenMap.find(appToken);
    if (appEntry == mAppTokenMap.end()) {
        ALOGE("Entry for %p contains unmapped application token %p", token.get(), appToken.get());
        return;
    }
    auto tokenEntry = std::find(appEntry->second.begin(), appEntry->second.end(), token);
    appEntry->second.erase(tokenEntry);
    // Stop listening for death if all operations tied to the token have finished.
    if (appEntry->second.size() == 0) {
        appToken->unlinkToDeath(mDeathRecipient);
        mAppTokenMap.erase(appEntry);
    }
}

bool OperationMap::hasPruneableOperation() {
    return mLru.size() != 0;
}

sp<IBinder> OperationMap::getOldestPruneableOperation() {
    if (!hasPruneableOperation()) {
        return sp<IBinder>(NULL);
    }
    return mLru[0];
}

std::vector<sp<IBinder>> OperationMap::getOperationsForToken(sp<IBinder> appToken) {
    auto appEntry = mAppTokenMap.find(appToken);
    if (appEntry != mAppTokenMap.end()) {
        return appEntry->second;
    } else {
        return std::vector<sp<IBinder>>();
    }
}

OperationMap::Operation::Operation(keymaster_operation_handle_t handle_,
                                   const keymaster1_device_t* device_,
                                   const keymaster_key_blob_t& key_,
                                   sp<IBinder> appToken_)
    : handle(handle_),
      device(device_),
      appToken(appToken_) {
          uint8_t* material = new uint8_t[key_.key_material_size];
          memcpy(material, key_.key_material, key_.key_material_size);
          key.key_material = material;
          key.key_material_size = key_.key_material_size;
}

OperationMap::Operation::Operation() : handle(0), device(NULL), appToken(NULL) {
    key.key_material = NULL;
    key.key_material_size = 0;
}
} // namespace android
