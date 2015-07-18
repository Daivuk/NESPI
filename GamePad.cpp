#include "GamePad.h"
#include <math.h>
#if defined(__GNUC__)
#include <memory.h>
#endif

#if defined(__GNUC__)
#define XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE  7849
#define XINPUT_GAMEPAD_RIGHT_THUMB_DEADZONE 8689
#define XINPUT_GAMEPAD_TRIGGER_THRESHOLD    30

#define XINPUT_GAMEPAD_DPAD_UP	        0x0001
#define XINPUT_GAMEPAD_DPAD_DOWN	    0x0002
#define XINPUT_GAMEPAD_DPAD_LEFT	    0x0004
#define XINPUT_GAMEPAD_DPAD_RIGHT	    0x0008
#define XINPUT_GAMEPAD_START	        0x0010
#define XINPUT_GAMEPAD_BACK	            0x0020
#define XINPUT_GAMEPAD_LEFT_THUMB	    0x0040
#define XINPUT_GAMEPAD_RIGHT_THUMB	    0x0080
#define XINPUT_GAMEPAD_LEFT_SHOULDER	0x0100
#define XINPUT_GAMEPAD_RIGHT_SHOULDER	0x0200
#define XINPUT_GAMEPAD_A	            0x1000
#define XINPUT_GAMEPAD_B	            0x2000
#define XINPUT_GAMEPAD_X	            0x4000
#define XINPUT_GAMEPAD_Y	            0x8000
#endif

namespace onut
{
    GamePad::GamePad(int index) :
        m_index(index)
    {
        memset(&m_currentState, 0, sizeof(m_currentState));
        memset(&m_previousState, 0, sizeof(m_previousState));
    }

    void GamePad::update()
    {
        m_previousState = m_currentState;
        memset(&m_currentState, 0, sizeof(m_currentState));
#if defined(__GNUC__)
        m_isConnected = false;
#else
        auto result = XInputGetState(m_index, &m_currentState);
        m_isConnected = result == ERROR_SUCCESS;
#endif

        // Update thumbs
        {
            static float deadZone = static_cast<float>(static_cast<double>(XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE) / 32768.0);

            m_cachedLeftThumb = {
                static_cast<float>(static_cast<double>(m_currentState.Gamepad.sThumbLX) / 32768.0),
                -static_cast<float>(static_cast<double>(m_currentState.Gamepad.sThumbLY) / 32768.0)};

            float len = sqrtf(m_cachedLeftThumb.x * m_cachedLeftThumb.x + m_cachedLeftThumb.y * m_cachedLeftThumb.y);
            if (len <= deadZone)
            {
                m_cachedLeftThumb = {};
            }
            else
            {
                float percent = (len - deadZone) / (1 - deadZone);
                m_cachedLeftThumb.x /= len;
                m_cachedLeftThumb.y /= len;
                m_cachedLeftThumb.x *= percent;
                m_cachedLeftThumb.y *= percent;
            }
        }

        {
            static float deadZone = static_cast<float>(static_cast<double>(XINPUT_GAMEPAD_RIGHT_THUMB_DEADZONE) / 32768.0);

            m_cachedRightThumb = {
                static_cast<float>(static_cast<double>(m_currentState.Gamepad.sThumbRX) / 32768.0),
                -static_cast<float>(static_cast<double>(m_currentState.Gamepad.sThumbRY) / 32768.0)};

            float len = sqrtf(m_cachedRightThumb.x * m_cachedRightThumb.x + m_cachedRightThumb.y * m_cachedRightThumb.y);
            if (len <= deadZone)
            {
                m_cachedRightThumb = {};
            }
            else
            {
                float percent = (len - deadZone) / (1 - deadZone);
                m_cachedRightThumb.x /= len;
                m_cachedRightThumb.y /= len;
                m_cachedRightThumb.x *= percent;
                m_cachedRightThumb.y *= percent;
            }
        }
    }

    bool GamePad::isConnected() const
    {
        return m_isConnected;
    }

    bool GamePad::isPressed(eGamePad button) const
    {
        return isPressed(button, m_currentState);
    }

    bool GamePad::isPressed(eGamePad button, const XINPUT_STATE& state) const
    {
        switch (button)
        {
            case A:
                return (state.Gamepad.wButtons & XINPUT_GAMEPAD_A) ? true : false;
            case B:
                return (state.Gamepad.wButtons & XINPUT_GAMEPAD_B) ? true : false;
            case X:
                return (state.Gamepad.wButtons & XINPUT_GAMEPAD_X) ? true : false;
            case Y:
                return (state.Gamepad.wButtons & XINPUT_GAMEPAD_Y) ? true : false;
            case DPAD_UP:
                return (state.Gamepad.wButtons & XINPUT_GAMEPAD_DPAD_UP) ? true : false;
            case DPAD_DOWN:
                return (state.Gamepad.wButtons & XINPUT_GAMEPAD_DPAD_DOWN) ? true : false;
            case DPAD_LEFT:
                return (state.Gamepad.wButtons & XINPUT_GAMEPAD_DPAD_LEFT) ? true : false;
            case DPAD_RIGHT:
                return (state.Gamepad.wButtons & XINPUT_GAMEPAD_DPAD_RIGHT) ? true : false;
            case LT:
                return (state.Gamepad.bLeftTrigger >= XINPUT_GAMEPAD_TRIGGER_THRESHOLD);
            case LB:
                return (state.Gamepad.wButtons & XINPUT_GAMEPAD_LEFT_SHOULDER) ? true : false;
            case RT:
                return (state.Gamepad.bRightTrigger >= XINPUT_GAMEPAD_TRIGGER_THRESHOLD);
            case RB:
                return (state.Gamepad.wButtons & XINPUT_GAMEPAD_RIGHT_SHOULDER) ? true : false;
            case LTHUMB:
                return (state.Gamepad.wButtons & XINPUT_GAMEPAD_LEFT_THUMB) ? true : false;
            case RTHUMB:
                return (state.Gamepad.wButtons & XINPUT_GAMEPAD_RIGHT_THUMB) ? true : false;
            case START:
                return (state.Gamepad.wButtons & XINPUT_GAMEPAD_START) ? true : false;
            case BACK:
                return (state.Gamepad.wButtons & XINPUT_GAMEPAD_BACK) ? true : false;
            case LTHUMB_LEFT:
                return (state.Gamepad.sThumbLX <= -XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE * 3);
            case LTHUMB_RIGHT:
                return (state.Gamepad.sThumbLX >= XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE * 3);
            case LTHUMB_UP:
                return (state.Gamepad.sThumbLY >= XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE * 3);
            case LTHUMB_DOWN:
                return (state.Gamepad.sThumbLY <= -XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE * 3);
            case RTHUMB_LEFT:
                return (state.Gamepad.sThumbLX <= -XINPUT_GAMEPAD_RIGHT_THUMB_DEADZONE * 3);
            case RTHUMB_RIGHT:
                return (state.Gamepad.sThumbLX >= XINPUT_GAMEPAD_RIGHT_THUMB_DEADZONE * 3);
            case RTHUMB_UP:
                return (state.Gamepad.sThumbLY >= XINPUT_GAMEPAD_RIGHT_THUMB_DEADZONE * 3);
            case RTHUMB_DOWN:
                return (state.Gamepad.sThumbLY <= -XINPUT_GAMEPAD_RIGHT_THUMB_DEADZONE * 3);
        }
        return false;
    }

    bool GamePad::isJustPressed(eGamePad button) const
    {
        return !isPressed(button, m_previousState) && isPressed(button, m_currentState);
    }

    bool GamePad::isJustReleased(eGamePad button) const
    {
        return isPressed(button, m_previousState) && !isPressed(button, m_currentState);
    }
}
