#pragma once

#include <unordered_map>
#include <cstdint>
#include <Utils/GameUtils/ChatUtils.hpp>

class Item;      // fwd
class ItemStack; // fwd
class Inventory; // fwd
class PlayerInventory; // fwd

class FastEat : public ModuleBase<FastEat> {
public:
    NumberSetting mSpeed = NumberSetting("Speed", "Speed multiplier for eating", 1, 1, 10, 1);

    FastEat()
        : ModuleBase("FastEat", "Allows you to eat food faster", ModuleCategory::Player, 0, false) {
        addSetting(&mSpeed);
        mNames = {
            {Lowercase, "fasteat"},
            {LowercaseSpaced, "fast eat"},
            {Normal, "FastEat"},
            {NormalSpaced, "Fast Eat"}
        };
    }

    void onInit() override;
    void onEnable() override;
    void onDisable() override;
    void onBaseTickEvent(class BaseTickEvent& event);

private:
    std::unordered_map<int, int> mOriginalDurations;
    bool mIsActive = false;

    void applyFastEat();
    void restoreNormalEat();
    int  getDesiredEatSpeed();

    static bool vIsFood(Item* item);                 // vfunc index 0x12
    static void vSetMaxUseDuration(Item* item, int); // vfunc index 0x1D
};
