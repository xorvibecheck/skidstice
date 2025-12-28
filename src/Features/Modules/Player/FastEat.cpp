#include "FastEat.hpp"
#include <cstdint>
#include <unordered_map>
#include <string>
#include <windows.h>
#include <SDK/Minecraft/Inventory/PlayerInventory.hpp>

static bool isExecPtr(void* p) {
    if (!p) return false;
    MEMORY_BASIC_INFORMATION mbi{};
    if (!VirtualQuery(p, &mbi, sizeof(mbi))) return false;
    const DWORD prot = mbi.Protect;
    return (prot & (PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY)) != 0;
}
template <typename Ret, typename... Args>
static inline bool vcall_seh(Ret& out, void* obj, size_t idx, Args... args) {
    if (!obj) return false;
    auto** vtbl = *reinterpret_cast<void***>(obj);
    if (!vtbl) return false;
    using Fn = Ret(*)(void*, Args...);
    __try {
        out = reinterpret_cast<Fn>(vtbl[idx])(obj, args...);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}
template <typename... Args>
static inline bool vcall_seh_void(void* obj, size_t idx, Args... args) {
    if (!obj) return false;
    auto** vtbl = *reinterpret_cast<void***>(obj);
    if (!vtbl) return false;
    using Fn = void(*)(void*, Args...);
    __try {
        reinterpret_cast<Fn>(vtbl[idx])(obj, args...);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

static int g_getIdx = -1;  // already found (you see "getIdx=5")
static int g_setIdx = -1;
static bool g_scannedSetter = false;

static int getMaxUse_auto(Item* item, ItemStack* stack) {
    if (!item) return 0;
    if (g_getIdx >= 0) {
        int v = 0;
        if (vcall_seh<int>(v, item, g_getIdx, stack)) return v;
        g_getIdx = -1;
    }
    const int candidates[] = { 0x4, 0x3, 0x5, 0x2, 0x6 };
    for (int idx : candidates) {
        int a = 0, b = 0;
        if (!vcall_seh<int>(a, item, idx, stack)) continue;
        if (!vcall_seh<int>(b, item, idx, stack)) continue;
        if (a == b && a >= 0 && a <= 256) {
            g_getIdx = idx;
            //ChatUtils::displayClientMessageRaw(
            //    ("§eFAST EAT §7getIdx=§6" + std::to_string(g_getIdx)).c_str());
            return a;
        }
    }
    return 0;
}

static bool findSetterByScan(Item* item, ItemStack* stack) {
    if (!item) return false;
    if (g_getIdx < 0) return false;

    int before = getMaxUse_auto(item, stack);
    if (before <= 0 || before > 256) return false;

    void*** pVtbl = reinterpret_cast<void***>(item);
    if (!pVtbl || !*pVtbl) return false;

    const int start = g_getIdx + 1;
    const int stop  = g_getIdx + 0x80;

    const int probe = 7;

    for (int idx = start; idx <= stop; ++idx) {
        void* fn = (*pVtbl)[idx];
        if (!isExecPtr(fn)) continue;

        if (!vcall_seh_void(item, idx, probe)) continue;

        int after = getMaxUse_auto(item, stack);

        vcall_seh_void(item, idx, before);

        if (after == probe && after != before) {
            g_setIdx = idx;
            //ChatUtils::displayClientMessageRaw(
            //    ("§eFAST EAT §7setIdx=§6" + std::to_string(g_setIdx)).c_str());
            return true;
        }
    }
    return false;
}

static void setMaxUse_safe(Item* item, ItemStack* stack, int ticks) {
    if (!item) return;
    if (g_setIdx < 0 && !g_scannedSetter) {
        g_scannedSetter = true;
        (void)findSetterByScan(item, stack);
    }
    if (g_setIdx >= 0) {
        (void)vcall_seh_void(item, g_setIdx, ticks); // guarded
    }
}

static bool ensureSetterIdx(Item* item, ItemStack* stack) {
    if (g_setIdx >= 0) return true;
    if (g_getIdx < 0) return false;

    int before = getMaxUse_auto(item, stack);
    if (before <= 0 || before > 256) return false;

    const int deltas[] = { 0x19, 0x18, 0x1A, 0x17, 0x1B };

    const int probeVal = 7;
    for (int d : deltas) {
        const int idx = g_getIdx + d;

        if (!vcall_seh_void(item, idx, probeVal))
            continue;

        int after = getMaxUse_auto(item, stack);

        vcall_seh_void(item, idx, before);

        if (after == probeVal && after != before) {
            g_setIdx = idx;
            //ChatUtils::displayClientMessageRaw(
            //    ("§gFAST EAT §7setIdx=§e" + std::to_string(g_setIdx)).c_str());
            return true;
        }
    }
    return false;
}


void FastEat::onInit() {}

void FastEat::onEnable() {
    mIsActive = true;
    mOriginalDurations.clear();
    g_getIdx = -1;
    g_setIdx = -1;
    gFeatureManager->mDispatcher->listen<BaseTickEvent, &FastEat::onBaseTickEvent>(this);
}

void FastEat::onDisable() {
    mIsActive = false;
    gFeatureManager->mDispatcher->deafen<BaseTickEvent, &FastEat::onBaseTickEvent>(this);
    restoreNormalEat();
    mOriginalDurations.clear();
}

void FastEat::onBaseTickEvent(BaseTickEvent&) {
    if (!mIsActive) return;
    applyFastEat();
}

void FastEat::applyFastEat() {
    auto* ci = ClientInstance::get(); if (!ci) return;
    auto* lp = ci->getLocalPlayer();  if (!lp) return;

    auto* supplies  = lp->getSupplies();        if (!supplies) return;
    auto* inventory = supplies->getContainer(); if (!inventory) return;

    const int desired = getDesiredEatSpeed();

    for (int i = 0; i < 9; ++i) {
        ItemStack* stack = inventory->getItem(i);
        if (!stack) continue;
        Item* item = stack->getItem();
        if (!item) continue;

        const int cur = getMaxUse_auto(item, stack);
        if (cur <= 0 || cur > 128) continue;

        if (!mOriginalDurations.count(i)) mOriginalDurations[i] = cur;

        setMaxUse_safe(item, stack, desired);
    }
}


void FastEat::restoreNormalEat() {
    auto* ci = ClientInstance::get(); if (!ci) return;
    auto* lp = ci->getLocalPlayer();  if (!lp) return;

    auto* supplies  = lp->getSupplies();        if (!supplies) return;
    auto* inventory = supplies->getContainer(); if (!inventory) return;

    for (int i = 0; i < 9; ++i) {
        ItemStack* stack = inventory->getItem(i);
        if (!stack) continue;

        Item* item = stack->getItem();
        if (!item) continue;

        const int cur = getMaxUse_auto(item, stack);
        if (cur <= 0 || cur > 128) continue;

        int restoreTo = 32;
        if (auto it = mOriginalDurations.find(i); it != mOriginalDurations.end())
            restoreTo = it->second;

        if (g_setIdx >= 0) setMaxUse_safe(item, stack, restoreTo);
    }
}

int FastEat::getDesiredEatSpeed() {
    const int s = mSpeed.as<int>();
    switch (s) {
        case 1:  return 30;
        case 2:  return 25;
        case 3:  return 20;
        case 4:  return 15;
        case 5:  return 10;
        case 6:  return 8;
        case 7:  return 6;
        case 8:  return 4;
        case 9:  return 2;
        case 10: return 1;
        default: return 32;
    }
}
