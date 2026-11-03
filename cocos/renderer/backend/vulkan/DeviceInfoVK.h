/****************************************************************************
 Vulkan backend: device capabilities.

 Game code still asks about capabilities in GL terms (Configuration checks
 GL extension names), so getExtension() returns the GL ES extension names
 whose functionality this backend provides.
 ****************************************************************************/
#pragma once

#include "../DeviceInfo.h"

#include <string>

CC_BACKEND_BEGIN

class DeviceVK;

class DeviceInfoVK : public DeviceInfo
{
public:
    explicit DeviceInfoVK(DeviceVK* device);

    bool init() override;
    const char* getVendor() const override { return _vendor.c_str(); }
    const char* getRenderer() const override { return _renderer.c_str(); }
    const char* getVersion() const override { return _version.c_str(); }
    const char* getExtension() const override { return _extensions.c_str(); }
    bool checkForFeatureSupported(FeatureType feature) override;

private:
    DeviceVK* _device = nullptr;
    bool _etc2 = false;
    bool _astc = false;
    std::string _vendor;
    std::string _renderer;
    std::string _version;
    std::string _extensions;
};

CC_BACKEND_END
