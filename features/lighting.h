#pragma once

namespace TankRC::Features {
class Lighting {
  public:
    void begin(int pin);
    void setFeatureEnabled(bool enabled);
    void update(bool requestedState);

  private:
    int pin_ = -1;
    bool featureEnabled_ = true;
};
}  // namespace TankRC::Features
