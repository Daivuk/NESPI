#pragma once
#if defined(__GNUC__)
#include <cinttypes>
#else
#include <Windows.h>
#include <Xinput.h>
#endif

#if defined(__GNUC__)
typedef struct _XINPUT_GAMEPAD {
  uint16_t  wButtons;
  uint8_t   bLeftTrigger;
  uint8_t   bRightTrigger;
  int16_t   sThumbLX;
  int16_t   sThumbLY;
  int16_t   sThumbRX;
  int16_t   sThumbRY;
} XINPUT_GAMEPAD;

typedef struct _XINPUT_STATE {
  uint32_t       dwPacketNumber;
  XINPUT_GAMEPAD Gamepad;
} XINPUT_STATE;
#endif

namespace onut
{
    class GamePad
    {
    public:
        enum eGamePad
        {
            A,
            B,
            X,
            Y,
            DPAD_UP,
            DPAD_DOWN,
            DPAD_LEFT,
            DPAD_RIGHT,
            LT,
            LB,
            RT,
            RB,
            LTHUMB,
            RTHUMB,
            START,
            BACK,
            LTHUMB_LEFT,
            LTHUMB_RIGHT,
            LTHUMB_UP,
            LTHUMB_DOWN,
            RTHUMB_LEFT,
            RTHUMB_RIGHT,
            RTHUMB_UP,
            RTHUMB_DOWN
        };

        GamePad(int index);

        struct Vector2
        {
            float x, y;
            Vector2() : x(0), y(0) {}
            Vector2(float in_x, float in_y) : x(in_x), y(in_y) {}
        };

        void            update();
        bool            isConnected() const;
        bool            isPressed(eGamePad button) const;
        bool            isJustPressed(eGamePad button) const;
        bool            isJustReleased(eGamePad button) const;
        const Vector2&  getLeftThumb() const { return m_cachedLeftThumb; }
        const Vector2&  getRightThumb() const { return m_cachedRightThumb; }

    private:
        bool isPressed(eGamePad button, const XINPUT_STATE& state) const;

#if defined(__GNUC__)
        XINPUT_STATE    m_accumState;
#endif
        XINPUT_STATE    m_currentState;
        XINPUT_STATE    m_previousState;
        Vector2         m_cachedLeftThumb;
        Vector2         m_cachedRightThumb;
        int             m_index = 0;
        bool            m_isConnected = false;
        
#if defined(__GNUC__)
        int             m_fileDescriptor;
#endif
    };
};
