#ifndef _DEVICE_STATE_H_
#define _DEVICE_STATE_H_

enum DeviceState {
    kDeviceStateUnknown,
    kDeviceStateStarting,
    kDeviceStateWifiConfiguring,
    kDeviceStateIdle,
    kDeviceStateConnecting,
    kDeviceStateListening,
    kDeviceStateSpeaking,
    kDeviceStateUpgrading,      // Reserved – Cloud OTA disabled; kept for enum stability
    kDeviceStateActivating,
    kDeviceStateAudioTesting,
    kDeviceStateFatalError       // Reserved – never entered; kept as sentinel
};

#endif // _DEVICE_STATE_H_ 