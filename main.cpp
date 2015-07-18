#if defined(__GNUC__)
#include "GLES/gl.h"
#include "EGL/egl.h"
#include "EGL/eglext.h"
#include <dirent.h>
#include <string.h>
#include "bcm_host.h"
#else
#include <Windows.h>
#include <gl\gl.h>
#include <gl\glu.h>
#include "dirent.h"
#endif
#include <cinttypes>
#include <algorithm>
#include <chrono>
#include "LodePNG.h"
#include "dfr.h"
#include "GamePad.h"
#include "color.h"
#include <vector>
#include <future>
#include <thread>
#include <list>
#include <mutex>
#include <fstream>
#include <cassert>

#define GL_CLAMP_TO_EDGE 0x812F

#if defined(__GNUC__)
EGLDisplay display;
EGLSurface surface;
EGLContext context;
#else
HGLRC hRC = nullptr;  // Permanent Rendering Context
HDC hDC = nullptr;  // Private GDI Device Context
HWND hWnd = nullptr; // Holds Our Window Handle
HINSTANCE hInstance;      // Holds The Instance Of The Application
#endif

bool isDirty = true;
int menuSelected = 0;
int menuHighlighted = -1;
int selectedGame[4] = {0};
onut::GamePad *pGamePad;
float targetScroll[3] = {0.f};
float scroll[3] = {0.f};

GLuint texBackground;
GLuint texGameViewBackground;
GLuint texIcons[4];
GLuint texMenuText[4];
GLuint texGameShadow;
GLuint texNoRecent;
GLuint texSearchKeyboard[38];
GLuint texColor;

RgbColor colors[12] = {
    {255, 132, 0},
    {0, 156, 255},
    {204, 0, 255},
    {253, 39, 240},
    {108, 0, 254},
    {88, 88, 88},
    {34, 175, 27},
    {214, 206, 0},
    {174, 35, 35},
    {255, 150, 75},
    {75, 255, 150},
    {150, 75, 255},
};
int option_color = 0;
int frame = 0;

#define MAX_LIVE_GAME_TEXTURES 40
struct sUniqueTexture
{
    GLuint texture = 0;
    GLuint *pTarget = nullptr;
    int frame = 0;
};
sUniqueTexture gameTextures[MAX_LIVE_GAME_TEXTURES];
int nextGameTexture = 0;
void markTextureUse(GLuint textureId)
{
    for (auto &gameTexture : gameTextures)
    {
        if (gameTexture.texture == textureId)
        {
            gameTexture.frame = frame;
        }
    }
}

struct sGame
{
    std::string name;
    std::string nameCaps;
    std::string filename;
    std::string imageFilename;
    GLuint texture = 0;
    GLuint textTexture = 0;
    bool bIsLoadingTexture = false;
    bool bLoaded = false;
    bool bIsLoadingTextTexture = false;
    bool bTextLoaded = false;
};
std::vector<sGame*> games;
std::vector<sGame*> recents;
std::vector<sGame*> searches;
std::vector<std::string> recentFilenames;

std::string searchText;
std::string oldSearchText;
GLuint texSearchText = 0;
bool bIsSearchTextLoading = false;

struct sTextureLoadRequest
{
    std::string filename;
    GLuint *pTarget;
    bool *pIsLoading;
    bool bIsText;
    int w;
    int h;
};
std::vector<sTextureLoadRequest> loadRequests;
std::mutex textureLoaderMutex;

std::mutex mainLoopMutex;

std::thread *pTextureLoader = nullptr;

// templated version of my_equal so it could work with both char and wchar_t
template<typename charT>
struct my_equal
{
    my_equal() {}
    bool operator()(charT ch1, charT ch2)
    {
        return ::toupper(ch1) == ::toupper(ch2);
    }
};

// find substring (case insensitive)
template<typename T>
int ci_find_substr(const T& str1, const T& str2, const std::locale& loc = std::locale())
{
    typename T::const_iterator it = std::search(str1.begin(), str1.end(),
                                                str2.begin(), str2.end(), my_equal<typename T::value_type>());
    if (it != str1.end()) return it - str1.begin();
    else return -1; // not found
}

void remStr(std::string &out, const std::string &toRem)
{
    auto pos = ci_find_substr(out, toRem);
    if ((decltype(std::string::npos))pos != std::string::npos)
    {
        out.erase(pos, toRem.size());
    }
}

void trim(std::string &out)
{
    auto pos = out.find_first_not_of(' ');
    if (pos == std::string::npos)
    {
        out = "";
        return;
    }
    if (pos > 0)
    {
        out.erase(0, pos);
    }
    pos = out.find_last_not_of(' ');
    if (pos < out.size() - 1)
    {
        out.erase(pos + 1, out.size() - 1 - pos);
    }
}


#define SCREEN_W 1920
#define SCREEN_H 1080

#if defined(__GNUC__)
#define REAL_SCREEN_W (SCREEN_W / 2)
#define REAL_SCREEN_H (SCREEN_H / 2)
#else
#define REAL_SCREEN_W (SCREEN_W * 2 / 3)
#define REAL_SCREEN_H (SCREEN_H * 2 / 3)
#endif
//#define REAL_SCREEN_W SCREEN_W
//#define REAL_SCREEN_H SCREEN_H

#define MENU_GAMES 0
#define MENU_RECENTS 1
#define MENU_SEARCH 2
#define MENU_OPTIONS 3

#define SIDE_BAR_SIZE 486

#define COL_COUNT 2

GLuint createTextureFromData(uint8_t *pData, int w, int h)
{
    GLuint texture;
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexImage2D(GL_TEXTURE_2D, 0, 4, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, pData);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    return texture;
}

GLuint createTextureFromFile(const std::string &filename)
{
    std::vector<unsigned char> image; //the raw pixels (holy crap that must be slow)
    unsigned int w, h;
    lodepng::decode(image, w, h, filename);
    uint8_t* in_pData = &(image[0]);

    // Pre multiplied
    auto pData = in_pData;
    for (int i = 0; i < (int)w * (int)h; ++i, pData += 4)
    {
        pData[0] = pData[0] * pData[3] / 255;
        pData[1] = pData[1] * pData[3] / 255;
        pData[2] = pData[2] * pData[3] / 255;
    }
    pData = in_pData;

    return createTextureFromData(pData, w, h);
}

template<typename Tstring>
GLuint createText(const Tstring &text, int w, int h, int pointSize, dfr::eAlign align = dfr::eAlign::ALIGN_LEFT)
{
    dfr::sImage textImage;
    textImage.width = w;
    textImage.height = h;
    textImage.pData = new unsigned char[textImage.width * textImage.height * 4];
    memset(textImage.pData, 0, textImage.width * textImage.height * 4);
    dfr::drawText(text, textImage, {"fonts/OpenSans-Regular.ttf", pointSize}, {true, align, 14, false});

    // Pre multiplied
    auto pData = textImage.pData;
    for (int i = 0; i < w * h; ++i, pData += 4)
    {
        pData[0] = pData[0] * pData[3] / 255;
        pData[1] = pData[1] * pData[3] / 255;
        pData[2] = pData[2] * pData[3] / 255;
    }

    auto ret = createTextureFromData(textImage.pData, textImage.width, textImage.height);
    delete[]textImage.pData;
    return ret;
}

unsigned char *pReadyData = nullptr;
unsigned int pReadyW;
unsigned int pReadyH;
sTextureLoadRequest *pReadyTextureRequest = nullptr;
volatile bool bDataReady = false;
bool bDone = false;
void startTextureLoader()
{
    pTextureLoader = new std::thread([]
    {
        std::vector<uint8_t> imageData;
        unsigned int w;
        unsigned int h;
        while (!bDone)
        {
            textureLoaderMutex.lock();
            if (!loadRequests.empty())
            {
                auto loadRequest = loadRequests.front();
                loadRequests.erase(loadRequests.begin());
                textureLoaderMutex.unlock();

                imageData.clear();
                if (loadRequest.bIsText)
                {
                    dfr::sImage textImage;
                    w = textImage.width = loadRequest.w;
                    h = textImage.height = loadRequest.h;
                    imageData.resize(textImage.width * textImage.height * 4);
                    textImage.pData = &imageData[0];
                    memset(textImage.pData, 0, textImage.width * textImage.height * 4);
                    dfr::drawText(loadRequest.filename, textImage, {"fonts/OpenSans-Regular.ttf", 48}, {false, dfr::eAlign::ALIGN_CENTER, 14, false});
                }
                else
                {
                    lodepng::decode(imageData, w, h, loadRequest.filename);
                }

                // Pre multiplied
                auto pData = &imageData[0];
                for (int i = 0; i < (int)w * (int)h; ++i, pData += 4)
                {
                    pData[0] = pData[0] * pData[3] / 255;
                    pData[1] = pData[1] * pData[3] / 255;
                    pData[2] = pData[2] * pData[3] / 255;
                }

                pReadyData = &imageData[0];
                pReadyW = w;
                pReadyH = h;
                pReadyTextureRequest = &loadRequest;
                bDataReady = true;
                while (bDataReady)
                {
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                }
            }
            else
            {
                textureLoaderMutex.unlock();
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    });
}

void splitString(std::vector<std::string> &elems, const std::string& in_string, char in_delimiter)
{
    elems.clear();
    unsigned int start = 0;
    unsigned int end = 0;
    for (; end < in_string.length(); ++end)
    {
        if (in_string[end] == in_delimiter)
        {
            if (end - start)
            {
                elems.push_back(in_string.substr(start, end - start));
            }
            start = end + 1;
        }
    }
    if (start < in_string.length())
    {
        if (end - start)
        {
            elems.push_back(in_string.substr(start, end - start));
        }
    }
}

void search(const std::string& in_toSearch)
{
    searches.clear();
    static std::string toSearch;
    toSearch = in_toSearch;
    trim(toSearch);
    static std::vector<std::string> words;
    splitString(words, toSearch, ' ');
    if (words.empty()) return;
    for (auto pGame : games)
    {
        bool bAccepted = true;
        for (auto &word : words)
        {
            if (pGame->nameCaps.find(word) == std::string::npos)
            {
                bAccepted = false;
            }
        }
        if (bAccepted)
            searches.push_back(pGame);
    }
}

bool update()
{
    static auto prevTime = std::chrono::steady_clock::now();
    auto cutTime = std::chrono::steady_clock::now();
    auto elapsed = cutTime - prevTime;
    prevTime = cutTime;
    auto dt = (float)(std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count()) / 1000.f;

    isDirty = false;
    if (frame == 0)
    {
        isDirty = true;
    }

    pGamePad->update();

    bool bUp = pGamePad->isJustPressed(onut::GamePad::eGamePad::DPAD_UP);
    bool bDown = pGamePad->isJustPressed(onut::GamePad::eGamePad::DPAD_DOWN);
    bool bLeft = pGamePad->isJustPressed(onut::GamePad::eGamePad::DPAD_LEFT);
    bool bRight = pGamePad->isJustPressed(onut::GamePad::eGamePad::DPAD_RIGHT);
    bool bAccept = pGamePad->isJustPressed(onut::GamePad::eGamePad::A);
    bool bBack = pGamePad->isJustPressed(onut::GamePad::eGamePad::B);

    static float fUpAnim = 0.f;
    static float fDownAnim = 0.f;
    static float fLeftAnim = 0.f;
    static float fRightAnim = 0.f;

    if (fUpAnim > 0.f)
    {
        isDirty = true;
        fUpAnim -= dt;
    }
    if (fDownAnim > 0.f)
    {
        isDirty = true;
        fDownAnim -= dt;
    }
    if (fLeftAnim > 0.f)
    {
        isDirty = true;
        fLeftAnim -= dt;
    }
    if (fRightAnim > 0.f)
    {
        isDirty = true;
        fRightAnim -= dt;
    }

    if (pGamePad->isPressed(onut::GamePad::eGamePad::LTHUMB_UP))
    {
        if (fUpAnim <= 0.f)
        {
            bUp = true;
            fUpAnim = .15f;
            fDownAnim = 0;
            fLeftAnim = 0;
            fRightAnim = 0;
        }
    }
    else if (pGamePad->isPressed(onut::GamePad::eGamePad::LTHUMB_DOWN))
    {
        if (fDownAnim <= 0.f)
        {
            bDown = true;
            fDownAnim = .15f;
            fUpAnim = 0;
            fLeftAnim = 0;
            fRightAnim = 0;
        }
    }
    else if (pGamePad->isPressed(onut::GamePad::eGamePad::LTHUMB_LEFT))
    {
        if (fLeftAnim <= 0.f)
        {
            bLeft = true;
            fLeftAnim = .15f;
            fUpAnim = 0;
            fDownAnim = 0;
            fRightAnim = 0;
        }
    }
    else if (pGamePad->isPressed(onut::GamePad::eGamePad::LTHUMB_RIGHT))
    {
        if (fRightAnim <= 0.f)
        {
            bRight = true;
            fRightAnim = .15f;
            fUpAnim = 0;
            fDownAnim = 0;
            fLeftAnim = 0;
        }
    }

    isDirty = isDirty || bUp || bDown || bLeft || bDown || bBack || bAccept || bDataReady;

    isDirty = isDirty ||
        pGamePad->isJustReleased(onut::GamePad::eGamePad::LTHUMB_UP) ||
        pGamePad->isJustReleased(onut::GamePad::eGamePad::LTHUMB_DOWN) ||
        pGamePad->isJustReleased(onut::GamePad::eGamePad::LTHUMB_LEFT) ||
        pGamePad->isJustReleased(onut::GamePad::eGamePad::LTHUMB_RIGHT) ||
        pGamePad->isJustReleased(onut::GamePad::eGamePad::A) ||
        pGamePad->isJustReleased(onut::GamePad::eGamePad::B);

    if (menuHighlighted != -1)
    {
        if (bUp)
        {
            menuHighlighted = menuSelected = std::max<>(MENU_GAMES, menuSelected - 1);
        }
        if (bDown)
        {
            menuHighlighted = menuSelected = std::min<>(MENU_OPTIONS, menuSelected + 1);
        }
        if (bRight || bAccept)
        {
            if (!(menuSelected == MENU_RECENTS &&
                recents.empty()))
            {
                menuHighlighted = -1;
            }
        }
    }
    else
    {
        if (bBack)
        {
            menuHighlighted = menuSelected;
        }
        else if (menuSelected == MENU_OPTIONS)
        {
            if (bAccept)
            {
                if (selectedGame[menuSelected] < 12)
                {
                    option_color = selectedGame[menuSelected];
                }
            }
            if (bUp)
            {
                if (selectedGame[menuSelected] > 5 &&
                    selectedGame[menuSelected] < 12)
                    selectedGame[menuSelected] -= 6;
            }
            if (bDown)
            {
                if (selectedGame[menuSelected] < 6)
                    selectedGame[menuSelected] += 6;
            }
            if (bLeft)
            {
                if (selectedGame[menuSelected] > 0 &&
                    selectedGame[menuSelected] < 6)
                    --selectedGame[menuSelected];
                else if (selectedGame[menuSelected] > 6 &&
                         selectedGame[menuSelected] < 12)
                    --selectedGame[menuSelected];
                else if (selectedGame[menuSelected] == 0 ||
                         selectedGame[menuSelected] == 6)
                    menuHighlighted = menuSelected;
            }
            if (bRight)
            {
                if (selectedGame[menuSelected] < 5)
                    ++selectedGame[menuSelected];
                else if (selectedGame[menuSelected] < 11 &&
                         selectedGame[menuSelected] > 5)
                    ++selectedGame[menuSelected];
            }
        }
        else if (menuSelected < MENU_OPTIONS)
        {
            if (bAccept)
            {
                if (menuSelected == MENU_SEARCH)
                {
                    if (selectedGame[menuSelected] < 38)
                    {
                        if (selectedGame[menuSelected] < 26)
                        {
                            searchText += (char)('A' + selectedGame[menuSelected]);
                        }
                        else if (selectedGame[menuSelected] < 36)
                        {
                            searchText += (char)('0' + (selectedGame[menuSelected] - 26));
                        }
                        else if (selectedGame[menuSelected] == 36)
                        {
                            searchText += ' ';
                        }
                        else if (selectedGame[menuSelected] == 37)
                        {
                            if (!searchText.empty())
                            {
                                searchText.resize(searchText.size() - 1);
                            }
                        }

                        // trim
                        searchText += 'A';
                        trim(searchText);
                        searchText.resize(searchText.size() - 1);

                        // Useless to search more than 64 characters
                        if (searchText.size() > 26) searchText.resize(26);

                        // Redo the search
                        search(searchText);
                    }
                }
            }
            auto colCount = COL_COUNT;
            if (menuSelected == MENU_SEARCH)
            {
                if (selectedGame[menuSelected] < 38)
                {
                    colCount = 13;
                }
            }
            if (bRight)
            {
                if ((selectedGame[menuSelected] + 1) % colCount)
                    selectedGame[menuSelected]++;
            }
            if (bLeft)
            {
                if (selectedGame[menuSelected] % colCount != 0)
                    selectedGame[menuSelected]--;
                else
                {
                    menuHighlighted = menuSelected;
                }
            }
            if (bDown)
            {
                int len = 0;
                if (menuSelected == MENU_GAMES)
                    len = (int)games.size();
                else if (menuSelected == MENU_RECENTS)
                    len = (int)recents.size();
                else if (menuSelected == MENU_SEARCH)
                    len = 38 + (int)searches.size();
                if (menuSelected == MENU_SEARCH &&
                    (selectedGame[menuSelected] == 23 ||
                    selectedGame[menuSelected] == 24))
                {
                    selectedGame[menuSelected] = 36;
                }
                else if (menuSelected == MENU_SEARCH &&
                         selectedGame[menuSelected] == 25)
                {
                    selectedGame[menuSelected] = 37;
                }
                else if (menuSelected == MENU_SEARCH &&
                         selectedGame[menuSelected] >= 26 &&
                         selectedGame[menuSelected] <= 32)
                {
                    if (len >= 38)
                        selectedGame[menuSelected] = 38;
                }
                else if (menuSelected == MENU_SEARCH &&
                         selectedGame[menuSelected] >= 32 &&
                         selectedGame[menuSelected] <= 37)
                {
                    selectedGame[menuSelected] = 37;
                    if (len >= 39)
                        selectedGame[menuSelected] = 39;
                    else if (len >= 38)
                        selectedGame[menuSelected] = 38;
                }
                else if (selectedGame[menuSelected] <= len - 1)
                {
                    selectedGame[menuSelected] += colCount;
                    if (selectedGame[menuSelected] >= len)
                        selectedGame[menuSelected] = len - 1;
                }
            }
            if (bUp)
            {
                if (menuSelected == MENU_SEARCH &&
                    selectedGame[menuSelected] == 37)
                {
                    selectedGame[menuSelected] = 25;
                }
                else if (menuSelected == MENU_SEARCH &&
                         selectedGame[menuSelected] == 38)
                {
                    selectedGame[menuSelected] = 28;
                }
                else if (menuSelected == MENU_SEARCH &&
                         selectedGame[menuSelected] == 39)
                {
                    selectedGame[menuSelected] = 34;
                }
                else if (selectedGame[menuSelected] >= colCount)
                {
                    selectedGame[menuSelected] -= colCount;
                    if (selectedGame[menuSelected] < 0)
                        selectedGame[menuSelected] = 0;
                }
            }
        }
    }

    for (int i = 0; i < MENU_OPTIONS; ++i)
    {
        int selectedId = selectedGame[i];
        if (menuSelected == MENU_SEARCH)
        {
            selectedId -= 38;
            if (selectedId < 0)
            {
                selectedId = 0;
            }
        }
        if (selectedId < COL_COUNT)
        {
            targetScroll[i] = 0.f;
        }
        else
        {
            targetScroll[i] = (float)((selectedId - 2) / COL_COUNT);
            targetScroll[i] *= 472.f;
            if (menuSelected == MENU_SEARCH)
            {
                targetScroll[i] += 384.f;
            }
            else
            {
                targetScroll[i] += 384.f / 2.f;
            }
            if (targetScroll[i] < 0) targetScroll[i] = 0;
        }
        if (scroll[i] < targetScroll[i])
        {
            isDirty = true;
            scroll[i] += std::max<>(32.f, (targetScroll[i] - scroll[i])) * dt * 5;
            if (scroll[i] > targetScroll[i]) scroll[i] = targetScroll[i];
        }
        if (scroll[i] > targetScroll[i])
        {
            isDirty = true;
            scroll[i] += std::min<>(-32.f, (targetScroll[i] - scroll[i])) * dt * 5;
            if (scroll[i] < targetScroll[i]) scroll[i] = targetScroll[i];
        }
    }

    return isDirty;
}

void draw()
{
    ++frame;
    if (bDataReady)
    {
        sUniqueTexture *pGameTexture = nullptr;
        int furthest = 0;
        for (auto &gameTexture : gameTextures)
        {
            if (!gameTexture.texture)
            {
                pGameTexture = &gameTexture;
                break;
            }
            else if (frame - gameTexture.frame > furthest)
            {
                pGameTexture = &gameTexture;
                furthest = frame - gameTexture.frame;
            }
        }

        if (!pGameTexture)
        {
            pGameTexture = gameTextures + nextGameTexture;
            nextGameTexture = (nextGameTexture + 1) % MAX_LIVE_GAME_TEXTURES;
        }
        if (pGameTexture->texture)
        {
            glDeleteTextures(1, &pGameTexture->texture);
        }
        if (pGameTexture->pTarget)
        {
            *pGameTexture->pTarget = 0;
        }
        pGameTexture->pTarget = pReadyTextureRequest->pTarget;
        *pGameTexture->pTarget = pGameTexture->texture = createTextureFromData(pReadyData, pReadyW, pReadyH);
        *pReadyTextureRequest->pIsLoading = false;
        pGameTexture->frame = frame;
        bDataReady = false;
    }

    auto hlsOption = RgbToHsv(RgbColor{colors[option_color].r, colors[option_color].g, colors[option_color].b});
    hlsOption.s = 127;
    auto selectedColor = HsvToRgb(hlsOption);

    // Background
    glDisable(GL_BLEND);
    glBindTexture(GL_TEXTURE_2D, texBackground);
  /*  glColor3ub(colors[option_color].r, colors[option_color].g, colors[option_color].b);
    glBegin(GL_QUADS);
    glTexCoord2f(0, 0);
    glVertex2f(0, 0);
    glTexCoord2f(0, 1);
    glVertex2f(0, SCREEN_H);
    glTexCoord2f(1, 1);
    glVertex2f(SCREEN_W, SCREEN_H);
    glTexCoord2f(1, 0);
    glVertex2f(SCREEN_W, 0);
    glEnd();*/

    // Side menu selection
    if (menuHighlighted != -1)
    {
        glDisable(GL_TEXTURE_2D);
     /*   glColor3ub(selectedColor.r, selectedColor.g, selectedColor.b);
        glBegin(GL_QUADS);
        glTexCoord2f(0, 0);
        glVertex2f(0, 304 + 130 * (float)menuHighlighted - 20);
        glTexCoord2f(0, 1);
        glVertex2f(0, 304 + 108 + 130 * (float)menuHighlighted - 20);
        glTexCoord2f(1, 1);
        glVertex2f(0 + SIDE_BAR_SIZE, 304 + 108 + 130 * (float)menuHighlighted - 20);
        glTexCoord2f(1, 0);
        glVertex2f(0 + SIDE_BAR_SIZE, 304 + 130 * (float)menuHighlighted - 20);
        glEnd();*/
    }

    // Shadow + main view
    glBindTexture(GL_TEXTURE_2D, texGameViewBackground);
    glEnable(GL_TEXTURE_2D);
    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
 /*   glBegin(GL_QUADS);
    glTexCoord2f(0, 0);
    glVertex2f(SIDE_BAR_SIZE, 0);
    glTexCoord2f(0, 1);
    glVertex2f(SIDE_BAR_SIZE, SCREEN_H);
    glTexCoord2f((SCREEN_W - SIDE_BAR_SIZE) / 16.f, 1);
    glVertex2f(SCREEN_W, SCREEN_H);
    glTexCoord2f((SCREEN_W - SIDE_BAR_SIZE) / 16.f, 0);
    glVertex2f(SCREEN_W, 0);
    glEnd();*/

#if _DEBUG
    static int anim = 0;
    anim++;
    if (anim == 16) anim = 0;
    glColor3f(1, 1, 1);
    glDisable(GL_TEXTURE_2D);
    glBegin(GL_QUADS);
    glVertex2f(SCREEN_W - (float)(anim % 4) * 4 - 4, (float)(anim / 4) * 4);
    glVertex2f(SCREEN_W - (float)(anim % 4) * 4 - 4, (float)(anim / 4) * 4 + 4);
    glVertex2f(SCREEN_W - (float)(anim % 4) * 4 - 4 + 4, (float)(anim / 4) * 4 + 4);
    glVertex2f(SCREEN_W - (float)(anim % 4) * 4 - 4 + 4, (float)(anim / 4) * 4);
    glEnd();
    glEnable(GL_TEXTURE_2D);
#endif

    // Side menu icons
    glBindTexture(GL_TEXTURE_2D, texIcons[MENU_GAMES]);
 /*   if (menuSelected == MENU_GAMES) glColor3ub(255, 255, 255);
    else glColor3ub(0, 0, 0);
    glBegin(GL_QUADS);
    glTexCoord2f(0, 0);
    glVertex2f(38, 304);
    glTexCoord2f(0, 1);
    glVertex2f(38, 304 + 68);
    glTexCoord2f(1, 1);
    glVertex2f(38 + 68, 304 + 68);
    glTexCoord2f(1, 0);
    glVertex2f(38 + 68, 304);
    glEnd();*/
    glBindTexture(GL_TEXTURE_2D, texMenuText[MENU_GAMES]);
  /*  glBegin(GL_QUADS);
    glTexCoord2f(0, 0);
    glVertex2f(126, 304);
    glTexCoord2f(0, 1);
    glVertex2f(126, 304 + 68);
    glTexCoord2f(1, 1);
    glVertex2f(126 + 334, 304 + 68);
    glTexCoord2f(1, 0);
    glVertex2f(126 + 334, 304);
    glEnd();*/

    glBindTexture(GL_TEXTURE_2D, texIcons[MENU_RECENTS]);
 /*   if (menuSelected == MENU_RECENTS) glColor3ub(255, 255, 255);
    else glColor3ub(0, 0, 0);
    glBegin(GL_QUADS);
    glTexCoord2f(0, 0);
    glVertex2f(38, 434);
    glTexCoord2f(0, 1);
    glVertex2f(38, 434 + 68);
    glTexCoord2f(1, 1);
    glVertex2f(38 + 68, 434 + 68);
    glTexCoord2f(1, 0);
    glVertex2f(38 + 68, 434);
    glEnd();*/
    glBindTexture(GL_TEXTURE_2D, texMenuText[MENU_RECENTS]);
  /*  glBegin(GL_QUADS);
    glTexCoord2f(0, 0);
    glVertex2f(126, 434);
    glTexCoord2f(0, 1);
    glVertex2f(126, 434 + 68);
    glTexCoord2f(1, 1);
    glVertex2f(126 + 334, 434 + 68);
    glTexCoord2f(1, 0);
    glVertex2f(126 + 334, 434);
    glEnd();*/

    glBindTexture(GL_TEXTURE_2D, texIcons[MENU_SEARCH]);
  /*  if (menuSelected == MENU_SEARCH) glColor3ub(255, 255, 255);
    else glColor3ub(0, 0, 0);
    glBegin(GL_QUADS);
    glTexCoord2f(0, 0);
    glVertex2f(38, 564);
    glTexCoord2f(0, 1);
    glVertex2f(38, 564 + 68);
    glTexCoord2f(1, 1);
    glVertex2f(38 + 68, 564 + 68);
    glTexCoord2f(1, 0);
    glVertex2f(38 + 68, 564);
    glEnd();*/
    glBindTexture(GL_TEXTURE_2D, texMenuText[MENU_SEARCH]);
  /*  glBegin(GL_QUADS);
    glTexCoord2f(0, 0);
    glVertex2f(126, 564);
    glTexCoord2f(0, 1);
    glVertex2f(126, 564 + 68);
    glTexCoord2f(1, 1);
    glVertex2f(126 + 334, 564 + 68);
    glTexCoord2f(1, 0);
    glVertex2f(126 + 334, 564);
    glEnd();*/

    glBindTexture(GL_TEXTURE_2D, texIcons[MENU_OPTIONS]);
 /*   if (menuSelected == MENU_OPTIONS) glColor3ub(255, 255, 255);
    else glColor3ub(0, 0, 0);
    glBegin(GL_QUADS);
    glTexCoord2f(0, 0);
    glVertex2f(38, 694);
    glTexCoord2f(0, 1);
    glVertex2f(38, 694 + 68);
    glTexCoord2f(1, 1);
    glVertex2f(38 + 68, 694 + 68);
    glTexCoord2f(1, 0);
    glVertex2f(38 + 68, 694);
    glEnd();*/
    glBindTexture(GL_TEXTURE_2D, texMenuText[MENU_OPTIONS]);
/*    glBegin(GL_QUADS);
    glTexCoord2f(0, 0);
    glVertex2f(126, 694);
    glTexCoord2f(0, 1);
    glVertex2f(126, 694 + 68);
    glTexCoord2f(1, 1);
    glVertex2f(126 + 334, 694 + 68);
    glTexCoord2f(1, 0);
    glVertex2f(126 + 334, 694);
    glEnd();*/

    // Draw search text
    if (menuSelected == MENU_SEARCH)
    {
        glDisable(GL_TEXTURE_2D);
   /*     glColor4f(0, 0, 0, .4f);
        glBegin(GL_QUADS);
        {
            glVertex2f(38, 64);
            glVertex2f(38, 64 + 68);
            glVertex2f(SIDE_BAR_SIZE - 38, 64 + 68);
            glVertex2f(SIDE_BAR_SIZE - 38, 64);

            glColor3ub(selectedColor.r, selectedColor.g, selectedColor.b);
            glVertex2f(38 - 4, 64);
            glVertex2f(38 - 4, 64 + 68);
            glVertex2f(38 - 1, 64 + 68);
            glVertex2f(38 - 1, 64);

            glVertex2f(SIDE_BAR_SIZE - 38 + 1, 64);
            glVertex2f(SIDE_BAR_SIZE - 38 + 1, 64 + 68);
            glVertex2f(SIDE_BAR_SIZE - 38 + 4, 64 + 68);
            glVertex2f(SIDE_BAR_SIZE - 38 + 4, 64);

            glVertex2f(38 - 4, 64 - 3);
            glVertex2f(38 - 4, 64);
            glVertex2f(SIDE_BAR_SIZE - 38 + 4, 64);
            glVertex2f(SIDE_BAR_SIZE - 38 + 4, 64 - 3);

            glVertex2f(38 - 4, 64 + 68);
            glVertex2f(38 - 4, 64 + 68 + 3);
            glVertex2f(SIDE_BAR_SIZE - 38 + 4, 64 + 68 + 3);
            glVertex2f(SIDE_BAR_SIZE - 38 + 4, 64 + 68);
        }
        glEnd();*/
        glEnable(GL_TEXTURE_2D);
     //   glColor3ub(255, 255, 255);
        if (texSearchText)
        {
            markTextureUse(texSearchText);
            glBindTexture(GL_TEXTURE_2D, texSearchText);
        /*    glBegin(GL_QUADS);
            glTexCoord2f(0, 0);
            glVertex2f(38, 64);
            glTexCoord2f(0, 1);
            glVertex2f(38, 64 + 68);
            glTexCoord2f(1, 1);
            glVertex2f(SIDE_BAR_SIZE - 38, 64 + 68);
            glTexCoord2f(1, 0);
            glVertex2f(SIDE_BAR_SIZE - 38, 64);
            glEnd();*/
        }
        if (texSearchText == 0 ||
            searchText != oldSearchText)
        {
            oldSearchText = searchText;
            if (!bIsSearchTextLoading)
            {
                bIsSearchTextLoading = true;

                if (!pTextureLoader)
                {
                    startTextureLoader();
                }
                textureLoaderMutex.lock();
                loadRequests.push_back({searchText, &texSearchText, &bIsSearchTextLoading, true, SIDE_BAR_SIZE - 38 - 38, 68});
                textureLoaderMutex.unlock();
            }
        }
    }

    // Draw settings
    if (menuSelected == MENU_OPTIONS)
    {
        glBindTexture(GL_TEXTURE_2D, texColor);
        glEnable(GL_TEXTURE_2D);
        for (int i = 0; i < 12; ++i)
        {
       /*     glColor3ub(colors[i].r, colors[i].g, colors[i].b);
            glBegin(GL_QUADS);
            glTexCoord2f(0, 0);
            glVertex2f(SIDE_BAR_SIZE + 134 + (float)(i % 6) * 205, 144 + (float)(i / 6) * 113);
            glTexCoord2f(0, 1);
            glVertex2f(SIDE_BAR_SIZE + 134 + (float)(i % 6) * 205, 144 + 109 + (float)(i / 6) * 113);
            glTexCoord2f(1, 1);
            glVertex2f(SIDE_BAR_SIZE + 134 + 186 + (float)(i % 6) * 205, 144 + 109 + (float)(i / 6) * 113);
            glTexCoord2f(1, 0);
            glVertex2f(SIDE_BAR_SIZE + 134 + 186 + (float)(i % 6) * 205, 144 + (float)(i / 6) * 113);
            glEnd();*/
            if (option_color == i)
            {
           //     glColor3ub(255, 255, 255);
                glDisable(GL_TEXTURE_2D);
            /*    glBegin(GL_QUADS);
                {
                    glVertex2f(SIDE_BAR_SIZE + 134 + (float)(i % 6) * 205,
                               144 + (float)(i / 6) * 113);
                    glVertex2f(SIDE_BAR_SIZE + 134 + (float)(i % 6) * 205,
                               144 + 80 + (float)(i / 6) * 113);
                    glVertex2f(SIDE_BAR_SIZE + 134 + (float)(i % 6) * 205 + 4,
                               144 + 80 + (float)(i / 6) * 113);
                    glVertex2f(SIDE_BAR_SIZE + 134 + (float)(i % 6) * 205 + 4,
                               144 + (float)(i / 6) * 113);
                    
                    glVertex2f(SIDE_BAR_SIZE + 134 + 170 + (float)(i % 6) * 205 - 4,
                               144 + (float)(i / 6) * 113);
                    glVertex2f(SIDE_BAR_SIZE + 134 + 170 + (float)(i % 6) * 205 - 4,
                               144 + 80 + (float)(i / 6) * 113);
                    glVertex2f(SIDE_BAR_SIZE + 134 + 170 + (float)(i % 6) * 205,
                               144 + 80 + (float)(i / 6) * 113);
                    glVertex2f(SIDE_BAR_SIZE + 134 + 170 + (float)(i % 6) * 205,
                               144 + (float)(i / 6) * 113);
                    
                    glVertex2f(SIDE_BAR_SIZE + 134 + (float)(i % 6) * 205,
                               144 + (float)(i / 6) * 113);
                    glVertex2f(SIDE_BAR_SIZE + 134 + (float)(i % 6) * 205,
                               144 + (float)(i / 6) * 113 + 4);
                    glVertex2f(SIDE_BAR_SIZE + 134 + 170 + (float)(i % 6) * 205,
                               144 + (float)(i / 6) * 113 + 4);
                    glVertex2f(SIDE_BAR_SIZE + 134 + 170 + (float)(i % 6) * 205,
                               144 + (float)(i / 6) * 113);

                    glVertex2f(SIDE_BAR_SIZE + 134 + (float)(i % 6) * 205,
                               144 + 80 + (float)(i / 6) * 113 - 5);
                    glVertex2f(SIDE_BAR_SIZE + 134 + (float)(i % 6) * 205,
                               144 + 80 + (float)(i / 6) * 113);
                    glVertex2f(SIDE_BAR_SIZE + 134 + 170 + (float)(i % 6) * 205,
                               144 + 80 + (float)(i / 6) * 113);
                    glVertex2f(SIDE_BAR_SIZE + 134 + 170 + (float)(i % 6) * 205,
                               144 + 80 + (float)(i / 6) * 113 - 5);
                }
                glEnd();*/
                glEnable (GL_TEXTURE_2D);
            }
            if (selectedGame[menuSelected] == i &&
                menuHighlighted == -1)
            {
                float padding = 12;
                if (pGamePad->isPressed(onut::GamePad::eGamePad::A))
                {
                    padding = 6;
                }
            //    glColor3ub(selectedColor.r, selectedColor.g, selectedColor.b);
                glDisable(GL_TEXTURE_2D);
             /*   glBegin(GL_QUADS);
                {
                    glVertex2f(SIDE_BAR_SIZE + 134 + (float)(i % 6) * 205 - padding,
                               144 + (float)(i / 6) * 113 - padding);
                    glVertex2f(SIDE_BAR_SIZE + 134 + (float)(i % 6) * 205 - padding,
                               144 + 80 + (float)(i / 6) * 113 + padding);
                    glVertex2f(SIDE_BAR_SIZE + 134 + (float)(i % 6) * 205,
                               144 + 80 + (float)(i / 6) * 113 + padding);
                    glVertex2f(SIDE_BAR_SIZE + 134 + (float)(i % 6) * 205,
                               144 + (float)(i / 6) * 113 - padding);

                    glVertex2f(SIDE_BAR_SIZE + 134 + 170 + (float)(i % 6) * 205,
                               144 + (float)(i / 6) * 113 - padding);
                    glVertex2f(SIDE_BAR_SIZE + 134 + 170 + (float)(i % 6) * 205,
                               144 + 80 + (float)(i / 6) * 113 + padding);
                    glVertex2f(SIDE_BAR_SIZE + 134 + 170 + (float)(i % 6) * 205 + padding,
                               144 + 80 + (float)(i / 6) * 113 + padding);
                    glVertex2f(SIDE_BAR_SIZE + 134 + 170 + (float)(i % 6) * 205 + padding,
                               144 + (float)(i / 6) * 113 - padding);

                    glVertex2f(SIDE_BAR_SIZE + 134 + (float)(i % 6) * 205,
                               144 + (float)(i / 6) * 113 - padding);
                    glVertex2f(SIDE_BAR_SIZE + 134 + (float)(i % 6) * 205 - padding,
                               144 + (float)(i / 6) * 113);
                    glVertex2f(SIDE_BAR_SIZE + 134 + 170 + (float)(i % 6) * 205,
                               144 + (float)(i / 6) * 113);
                    glVertex2f(SIDE_BAR_SIZE + 134 + 170 + (float)(i % 6) * 205,
                               144 + (float)(i / 6) * 113 - padding);

                    glVertex2f(SIDE_BAR_SIZE + 134 + (float)(i % 6) * 205,
                               144 + 80 + (float)(i / 6) * 113);
                    glVertex2f(SIDE_BAR_SIZE + 134 + (float)(i % 6) * 205 - padding,
                               144 + 80 + (float)(i / 6) * 113 + padding);
                    glVertex2f(SIDE_BAR_SIZE + 134 + 170 + (float)(i % 6) * 205,
                               144 + 80 + (float)(i / 6) * 113 + padding);
                    glVertex2f(SIDE_BAR_SIZE + 134 + 170 + (float)(i % 6) * 205,
                               144 + 80 + (float)(i / 6) * 113);
                }
                glEnd();*/
                glEnable (GL_TEXTURE_2D);
            }
        }
    }

    // Draw the list items
    // 666x394
    if (menuSelected == MENU_RECENTS &&
        recents.empty())
    {
     //   glColor3ub(255, 255, 255);
        glBindTexture(GL_TEXTURE_2D, texNoRecent);
     /*   glBegin(GL_QUADS);
        glTexCoord2f(0, 0);
        glVertex2f(SIDE_BAR_SIZE + (SCREEN_W - SIDE_BAR_SIZE) / 2 - 400 / 2, SCREEN_H / 2 - 68 / 2);
        glTexCoord2f(0, 1);
        glVertex2f(SIDE_BAR_SIZE + (SCREEN_W - SIDE_BAR_SIZE) / 2 - 400 / 2, SCREEN_H / 2 - 68 / 2 + 68);
        glTexCoord2f(1, 1);
        glVertex2f(SIDE_BAR_SIZE + (SCREEN_W - SIDE_BAR_SIZE) / 2 - 400 / 2 + 400, SCREEN_H / 2 - 68 / 2 + 68);
        glTexCoord2f(1, 0);
        glVertex2f(SIDE_BAR_SIZE + (SCREEN_W - SIDE_BAR_SIZE) / 2 - 400 / 2 + 400, SCREEN_H / 2 - 68 / 2);
        glEnd();*/
    }
    if (menuSelected < MENU_OPTIONS)
    {
     //   glColor3ub(255, 255, 255);

        if (menuSelected == MENU_SEARCH)
        {
#define KEYBOARD_SPACING 104
#define KEYBOARD_COL_COUNT 13
            for (int c = 0; c < 38; ++c)
            {
                if (c == selectedGame[MENU_SEARCH] &&
                    menuHighlighted == -1)
                {
                //    glColor3ub(selectedColor.r, selectedColor.g, selectedColor.b);
                    glDisable(GL_TEXTURE_2D);
                    float offset = 8;
                    if (pGamePad->isPressed(onut::GamePad::eGamePad::A))
                    {
                        offset = 0;
                    }
                    if (c == 36)
                    {
                    /*    glBegin(GL_QUADS);
                        glVertex2f(64 + SIDE_BAR_SIZE + (float)(c % KEYBOARD_COL_COUNT) * KEYBOARD_SPACING - offset,
                                   (float)(c / KEYBOARD_COL_COUNT) * KEYBOARD_SPACING + 64 - offset);
                        glVertex2f(64 + SIDE_BAR_SIZE + (float)(c % KEYBOARD_COL_COUNT) * KEYBOARD_SPACING - offset,
                                   (float)(c / KEYBOARD_COL_COUNT) * KEYBOARD_SPACING + 64 + 64 + offset);
                        glVertex2f(64 + SIDE_BAR_SIZE + (float)(c % KEYBOARD_COL_COUNT) * KEYBOARD_SPACING + 64 + KEYBOARD_SPACING + offset,
                                   (float)(c / KEYBOARD_COL_COUNT) * KEYBOARD_SPACING + 64 + 64 + offset);
                        glVertex2f(64 + SIDE_BAR_SIZE + (float)(c % KEYBOARD_COL_COUNT) * KEYBOARD_SPACING + 64 + KEYBOARD_SPACING + offset,
                                   (float)(c / KEYBOARD_COL_COUNT) * KEYBOARD_SPACING + 64 - offset);
                        glEnd();*/
                    }
                    else if (c == 37)
                    {
                    /*    glBegin(GL_QUADS);
                        glVertex2f(64 + SIDE_BAR_SIZE + (float)((c + 1) % KEYBOARD_COL_COUNT) * KEYBOARD_SPACING - offset,
                                   (float)((c + 1) / KEYBOARD_COL_COUNT) * KEYBOARD_SPACING + 64 - offset);
                        glVertex2f(64 + SIDE_BAR_SIZE + (float)((c + 1) % KEYBOARD_COL_COUNT) * KEYBOARD_SPACING - offset,
                                   (float)((c + 1) / KEYBOARD_COL_COUNT) * KEYBOARD_SPACING + 64 + 64 + offset);
                        glVertex2f(64 + SIDE_BAR_SIZE + (float)((c + 1) % KEYBOARD_COL_COUNT) * KEYBOARD_SPACING + 64 + offset,
                                   (float)((c + 1) / KEYBOARD_COL_COUNT) * KEYBOARD_SPACING + 64 + 64 + offset);
                        glVertex2f(64 + SIDE_BAR_SIZE + (float)((c + 1) % KEYBOARD_COL_COUNT) * KEYBOARD_SPACING + 64 + offset,
                                   (float)((c + 1) / KEYBOARD_COL_COUNT) * KEYBOARD_SPACING + 64 - offset);
                        glEnd();*/
                    }
                    else
                    {
                   /*     glBegin(GL_QUADS);
                        glVertex2f(64 + SIDE_BAR_SIZE + (float)(c % KEYBOARD_COL_COUNT) * KEYBOARD_SPACING - offset,
                                   (float)(c / KEYBOARD_COL_COUNT) * KEYBOARD_SPACING + 64 - offset);
                        glVertex2f(64 + SIDE_BAR_SIZE + (float)(c % KEYBOARD_COL_COUNT) * KEYBOARD_SPACING - offset,
                                   (float)(c / KEYBOARD_COL_COUNT) * KEYBOARD_SPACING + 64 + 64 + offset);
                        glVertex2f(64 + SIDE_BAR_SIZE + (float)(c % KEYBOARD_COL_COUNT) * KEYBOARD_SPACING + 64 + offset,
                                   (float)(c / KEYBOARD_COL_COUNT) * KEYBOARD_SPACING + 64 + 64 + offset);
                        glVertex2f(64 + SIDE_BAR_SIZE + (float)(c % KEYBOARD_COL_COUNT) * KEYBOARD_SPACING + 64 + offset,
                                   (float)(c / KEYBOARD_COL_COUNT) * KEYBOARD_SPACING + 64 - offset);
                        glEnd();*/
                    }
                    if (pGamePad->isPressed(onut::GamePad::eGamePad::A))
                    {
                    //    glColor3ub(0, 0, 0);
                    }
                    else
                    {
                    //    glColor3ub(255, 255, 255);
                    }
                    glEnable(GL_TEXTURE_2D);
                }
                else
                {
                //    glColor3ub(255, 255, 255);
                }
                glBindTexture(GL_TEXTURE_2D, texSearchKeyboard[c]);
                if (c == 36)
                {
                 /*   glBegin(GL_QUADS);
                    glTexCoord2f(0, 0);
                    glVertex2f(64 + SIDE_BAR_SIZE + (float)(c % KEYBOARD_COL_COUNT) * KEYBOARD_SPACING, (float)(c / KEYBOARD_COL_COUNT) * KEYBOARD_SPACING + 64);
                    glTexCoord2f(0, 1);
                    glVertex2f(64 + SIDE_BAR_SIZE + (float)(c % KEYBOARD_COL_COUNT) * KEYBOARD_SPACING, (float)(c / KEYBOARD_COL_COUNT) * KEYBOARD_SPACING + 64 + 64);
                    glTexCoord2f(1, 1);
                    glVertex2f(64 + SIDE_BAR_SIZE + (float)(c % KEYBOARD_COL_COUNT) * KEYBOARD_SPACING + 64 + KEYBOARD_SPACING, (float)(c / KEYBOARD_COL_COUNT) * KEYBOARD_SPACING + 64 + 64);
                    glTexCoord2f(1, 0);
                    glVertex2f(64 + SIDE_BAR_SIZE + (float)(c % KEYBOARD_COL_COUNT) * KEYBOARD_SPACING + 64 + KEYBOARD_SPACING, (float)(c / KEYBOARD_COL_COUNT) * KEYBOARD_SPACING + 64);
                    glEnd();*/
                }
                else if (c == 37)
                {
                 /*   glBegin(GL_QUADS);
                    glTexCoord2f(0, 0);
                    glVertex2f(64 + SIDE_BAR_SIZE + (float)((c + 1) % KEYBOARD_COL_COUNT) * KEYBOARD_SPACING, (float)((c + 1) / KEYBOARD_COL_COUNT) * KEYBOARD_SPACING + 64);
                    glTexCoord2f(0, 1);
                    glVertex2f(64 + SIDE_BAR_SIZE + (float)((c + 1) % KEYBOARD_COL_COUNT) * KEYBOARD_SPACING, (float)((c + 1) / KEYBOARD_COL_COUNT) * KEYBOARD_SPACING + 64 + 64);
                    glTexCoord2f(1, 1);
                    glVertex2f(64 + SIDE_BAR_SIZE + (float)((c + 1) % KEYBOARD_COL_COUNT) * KEYBOARD_SPACING + 64, (float)((c + 1) / KEYBOARD_COL_COUNT) * KEYBOARD_SPACING + 64 + 64);
                    glTexCoord2f(1, 0);
                    glVertex2f(64 + SIDE_BAR_SIZE + (float)((c + 1) % KEYBOARD_COL_COUNT) * KEYBOARD_SPACING + 64, (float)((c + 1) / KEYBOARD_COL_COUNT) * KEYBOARD_SPACING + 64);
                    glEnd();*/
                }
                else
                {
                 /*   glBegin(GL_QUADS);
                    glTexCoord2f(0, 0);
                    glVertex2f(64 + SIDE_BAR_SIZE + (float)(c % KEYBOARD_COL_COUNT) * KEYBOARD_SPACING, (float)(c / KEYBOARD_COL_COUNT) * KEYBOARD_SPACING + 64);
                    glTexCoord2f(0, 1);
                    glVertex2f(64 + SIDE_BAR_SIZE + (float)(c % KEYBOARD_COL_COUNT) * KEYBOARD_SPACING, (float)(c / KEYBOARD_COL_COUNT) * KEYBOARD_SPACING + 64 + 64);
                    glTexCoord2f(1, 1);
                    glVertex2f(64 + SIDE_BAR_SIZE + (float)(c % KEYBOARD_COL_COUNT) * KEYBOARD_SPACING + 64, (float)(c / KEYBOARD_COL_COUNT) * KEYBOARD_SPACING + 64 + 64);
                    glTexCoord2f(1, 0);
                    glVertex2f(64 + SIDE_BAR_SIZE + (float)(c % KEYBOARD_COL_COUNT) * KEYBOARD_SPACING + 64, (float)(c / KEYBOARD_COL_COUNT) * KEYBOARD_SPACING + 64);
                    glEnd();*/
                }
            }
        }

        glPushMatrix();
        glTranslatef(0, -(float)(int)scroll[menuSelected], 0);
        int i = std::max<>(0, selectedGame[menuSelected] - 5);
        int x = i % COL_COUNT;
        int y = i / COL_COUNT;
        int selectedId = selectedGame[menuSelected];
        int limitId = selectedId;
        int len = 0;
        if (menuSelected == MENU_GAMES)
            len = (int)games.size();
        else if (menuSelected == MENU_RECENTS)
            len = (int)recents.size();
        else if (menuSelected == MENU_SEARCH)
        {
            len = (int)searches.size();
            glTranslatef(0, 384.f, 0);
            glEnable(GL_SCISSOR_TEST);
            glScissor(0, 0, REAL_SCREEN_W, REAL_SCREEN_H - (GLsizei)(384.f * ((float)REAL_SCREEN_H / (float)SCREEN_H)));
            i = std::max<>(0, selectedGame[menuSelected] - 38 - 5);
            selectedId = selectedGame[menuSelected] - 38;
            limitId = std::max<>(0, selectedId);
            x = i % COL_COUNT;
            y = i / COL_COUNT;
        }
        for (; i <= limitId + 5 && i < len; ++i)
        {
            sGame *pGame = nullptr;
            if (menuSelected == MENU_GAMES)
                pGame = games[i];
            else if (menuSelected == MENU_RECENTS)
                pGame = recents[i];
            else if (menuSelected == MENU_SEARCH)
            {
                pGame = searches[i];
            }
            if (!pGame) continue;

            if (pGame->texture)
            {
                glBindTexture(GL_TEXTURE_2D, texGameShadow);
             /*   glBegin(GL_QUADS);
                glTexCoord2f(0, 0);
                glVertex2f(534 + (float)x * 686, 50 + (float)y * 472);
                glTexCoord2f(0, 1);
                glVertex2f(534 + (float)x * 686, 50 + 394 + (float)y * 472);
                glTexCoord2f(1, 1);
                glVertex2f(534 + 666 + (float)x * 686, 50 + 394 + (float)y * 472);
                glTexCoord2f(1, 0);
                glVertex2f(534 + 666 + (float)x * 686, 50 + (float)y * 472);
                glEnd();*/
            }
            if (menuHighlighted == -1 && selectedId == i)
            {
           //     glColor3ub(selectedColor.r, selectedColor.g, selectedColor.b);
                glDisable(GL_TEXTURE_2D);
          /*      glBegin(GL_QUADS);
                glVertex2f(534 + (float)x * 686 - 12, 50 + (float)y * 472 - 12);
                glVertex2f(534 + (float)x * 686 - 12, 50 + 384 + (float)y * 472 + 78);
                glVertex2f(534 + 652 + (float)x * 686 + 12, 50 + 384 + (float)y * 472 + 78);
                glVertex2f(534 + 652 + (float)x * 686 + 12, 50 + (float)y * 472 - 12);
                glEnd();*/
                glEnable(GL_TEXTURE_2D);
          //      glColor3ub(255, 255, 255);

                // Draw text
                if (pGame->textTexture == 0)
                {
                    if (!pGame->bIsLoadingTextTexture)
                    {
                        pGame->bIsLoadingTextTexture = true;

                        if (!pTextureLoader)
                        {
                            startTextureLoader();
                        }
                        textureLoaderMutex.lock();
                        loadRequests.push_back({pGame->name, &pGame->textTexture, &pGame->bIsLoadingTextTexture, true, 652, 68});
                        textureLoaderMutex.unlock();
                    }
                }
                else
                {
              //      glColor3ub(0, 0, 0);
                    glBindTexture(GL_TEXTURE_2D, pGame->textTexture);
              /*      glBegin(GL_QUADS);
                    glTexCoord2f(0, 0);
                    glVertex2f(534 + (float)x * 686, 50 + (float)y * 472 + 384);
                    glTexCoord2f(0, 1);
                    glVertex2f(534 + (float)x * 686, 50 + 68 + (float)y * 472 + 384);
                    glTexCoord2f(1, 1);
                    glVertex2f(534 + 652 + (float)x * 686, 50 + 68 + (float)y * 472 + 384);
                    glTexCoord2f(1, 0);
                    glVertex2f(534 + 652 + (float)x * 686, 50 + (float)y * 472 + 384);
                    glEnd();
                    glColor3ub(255, 255, 255);*/
                }
            }
            if (pGame->texture == 0)
            {
                if (!pGame->bIsLoadingTexture)
                {
                    pGame->bIsLoadingTexture = true;

                    if (!pTextureLoader)
                    {
                        startTextureLoader();
                    }
                    textureLoaderMutex.lock();
                    loadRequests.push_back({pGame->imageFilename, &pGame->texture, &pGame->bIsLoadingTexture, false, 0, 0});
                    textureLoaderMutex.unlock();
                }
            }
            if (pGame->texture)
            {
                markTextureUse(pGame->texture);
                glBindTexture(GL_TEXTURE_2D, pGame->texture);
             /*   glBegin(GL_QUADS);
                glTexCoord2f(0, 0);
                glVertex2f(534 + (float)x * 686, 50 + (float)y * 472);
                glTexCoord2f(0, 1);
                glVertex2f(534 + (float)x * 686, 50 + 384 + (float)y * 472);
                glTexCoord2f(1, 1);
                glVertex2f(534 + 652 + (float)x * 686, 50 + 384 + (float)y * 472);
                glTexCoord2f(1, 0);
                glVertex2f(534 + 652 + (float)x * 686, 50 + (float)y * 472);
                glEnd();*/
            }

            x = (x + 1) % COL_COUNT;
            if (x == 0) ++y;
        }
        if (menuSelected == MENU_SEARCH)
        {
            glScissor(0, 0, REAL_SCREEN_W, REAL_SCREEN_H);
            glDisable(GL_SCISSOR_TEST);
        }
        glPopMatrix();
    }

#if defined(__GNUC__)
    eglSwapBuffers(display, surface);
#else
    SwapBuffers(hDC);
#endif
}

void loadResources()
{
    pGamePad = new onut::GamePad(0);

    texBackground = createTextureFromFile("background.png");
    texGameViewBackground = createTextureFromFile("gameViewBackground.png");
    texGameShadow = createTextureFromFile("gameShadow.png");
    texColor = createTextureFromFile("colorPicker.png");

    texIcons[MENU_GAMES] = createTextureFromFile("iconJeux.png");
    texIcons[MENU_RECENTS] = createTextureFromFile("iconRecent.png");
    texIcons[MENU_SEARCH] = createTextureFromFile("iconSearch.png");
    texIcons[MENU_OPTIONS] = createTextureFromFile("iconOptions.png");

    dfr::init();
    texMenuText[MENU_GAMES] = createText(L"Jeux", 334, 68, 48);
    texMenuText[MENU_RECENTS] = createText(L"Récents", 334, 68, 48);
    texMenuText[MENU_SEARCH] = createText(L"Recherche", 334, 68, 48);
    texMenuText[MENU_OPTIONS] = createText(L"Paramètres", 334, 68, 48);

    texNoRecent = createText(L"Aucun jeu joué récemment", 400, 68, 48, dfr::eAlign::ALIGN_CENTER);

    for (auto c = 'A'; c <= 'Z'; ++c)
    {
        texSearchKeyboard[c - 'A'] = createText(std::string() + c, 64, 64, 48, dfr::eAlign::ALIGN_CENTER);
    }
    for (auto c = '0'; c <= '9'; ++c)
    {
        texSearchKeyboard['Z' - 'A' + 1 + (c - '0')] = createText(std::string() + c, 64, 64, 48, dfr::eAlign::ALIGN_CENTER);
    }
    texSearchKeyboard[36] = createText("SPACE", 128 + KEYBOARD_SPACING - 64, 64, 48, dfr::eAlign::ALIGN_CENTER);
    texSearchKeyboard[37] = createTextureFromFile("backspace.png");

    // Load all games
    DIR *dir;
    struct dirent *ent;
    std::string lookIn = "./Games/";
    int count = 0;
    if ((dir = opendir(lookIn.c_str())) != NULL)
    {
        while ((ent = readdir(dir)) != NULL)
        {
            if (!strcmp(ent->d_name, "."))
            {
                continue;
            }
            else if (!strcmp(ent->d_name, ".."))
            {
                continue;
            }

            auto len = strlen(ent->d_name);
            if (len < 5) continue;
#if defined(__GNUC__)
            else if (strcasecmp(ent->d_name + len - 4, ".nes"))
#else
            else if (_stricmp(ent->d_name + len - 4, ".nes"))
#endif
            {
                continue;
            }

            std::string filename = ent->d_name;
            std::string fullPath = lookIn + filename;
            std::string picFilename = fullPath.substr(0, fullPath.size() - 3) + "png";

            sGame *pGame = new sGame();
            pGame->name = filename;

            remStr(pGame->name, ".nes");
            remStr(pGame->name, "(e)");
            remStr(pGame->name, "(u)");
            remStr(pGame->name, "(ue)");
            remStr(pGame->name, "(j)");
            remStr(pGame->name, "(je)");
            remStr(pGame->name, "(ju)");
            remStr(pGame->name, "(jue)");
            remStr(pGame->name, "[!]");
            remStr(pGame->name, "[o1]");
            remStr(pGame->name, "(taito)");
            remStr(pGame->name, "(tengen)");
            remStr(pGame->name, "(PRG0)");
            remStr(pGame->name, "(PRG1)");
            trim(pGame->name);

            if (pGame->name.find(", The") != std::string::npos)
            {
                pGame->name = "The " + pGame->name.substr(0, pGame->name.size() - 5);
            }
            pGame->nameCaps = pGame->name;
            for (auto &c : pGame->nameCaps)
            {
                c = ::toupper(c);
            }

            pGame->filename = filename;
            pGame->imageFilename = picFilename;
            games.push_back(pGame);

            ++count;
        }
        closedir(dir);
    }

    // Load recents
    std::ifstream in("recents.txt");
    if (!in.fail())
    {
        std::string line;
        std::getline(in, line);
        while (!in.eof())
        {
            recentFilenames.push_back(line);
            std::getline(in, line);
        }
        in.close();
    }
    for (auto &filename : recentFilenames)
    {
        for (auto pGame : games)
        {
            if (pGame->filename == filename)
            {
                recents.push_back(pGame);
                break;
            }
        }
    }
    if (recents.empty())
    {
        menuSelected = MENU_GAMES;
    }
    else
    {
        menuSelected = MENU_RECENTS;
    }
}

#if !defined(__GNUC__)
LRESULT CALLBACK WinProc(HWND handle, UINT msg, WPARAM wparam, LPARAM lparam)
{
    if (msg == WM_DESTROY ||
        msg == WM_CLOSE)
    {
        PostQuitMessage(0);
        return 0;
    }

    return DefWindowProc(handle, msg, wparam, lparam);
}
#endif

#if defined(__GNUC__)
int main()
#else
int CALLBACK WinMain(
    _In_  HINSTANCE hInstance,
    _In_  HINSTANCE hPrevInstance,
    _In_  LPSTR lpCmdLine,
    _In_  int nCmdShow
    )
#endif
{
#if defined(__GNUC__)
    bcm_host_init();
    
    EGLBoolean result;
    EGLint num_config;

    static EGL_DISPMANX_WINDOW_T nativewindow;

    DISPMANX_ELEMENT_HANDLE_T dispman_element;
    DISPMANX_DISPLAY_HANDLE_T dispman_display;
    DISPMANX_UPDATE_HANDLE_T dispman_update;
    VC_RECT_T dst_rect;
    VC_RECT_T src_rect;

    static const EGLint attribute_list[] =
    {
        EGL_RED_SIZE, 8,
        EGL_GREEN_SIZE, 8,
        EGL_BLUE_SIZE, 8,
        EGL_ALPHA_SIZE, 8,
        EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
        EGL_NONE
    };

    EGLConfig config;

    // get an EGL display connection
    display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    assert(display != EGL_NO_DISPLAY);

    // initialize the EGL display connection
    result = eglInitialize(display, NULL, NULL);
    assert(EGL_FALSE != result);

    // get an appropriate EGL frame buffer configuration
    result = eglChooseConfig(display, attribute_list, &config, 1, &num_config);
    assert(EGL_FALSE != result);

    // create an EGL rendering context
    context = eglCreateContext(display, config, EGL_NO_CONTEXT, NULL);
    assert(context != EGL_NO_CONTEXT);

    // create an EGL window surface

    dst_rect.x = 0;
    dst_rect.y = 0;
    dst_rect.width = REAL_SCREEN_W;
    dst_rect.height = REAL_SCREEN_H;

    src_rect.x = 0;
    src_rect.y = 0;
    src_rect.width = REAL_SCREEN_W << 16;
    src_rect.height = REAL_SCREEN_H << 16;        

    dispman_display = vc_dispmanx_display_open(0 /* LCD */);
    dispman_update = vc_dispmanx_update_start(0);
     
    dispman_element = vc_dispmanx_element_add(dispman_update, dispman_display,
        0/*layer*/, &dst_rect, 0/*src*/,
        &src_rect, DISPMANX_PROTECTION_NONE, 0 /*alpha*/, 0/*clamp*/, (DISPMANX_TRANSFORM_T)0/*transform*/);

    nativewindow.element = dispman_element;
    nativewindow.width = SCREEN_W;
    nativewindow.height = SCREEN_H;
    vc_dispmanx_update_submit_sync(dispman_update);

    surface = eglCreateWindowSurface(display, config, &nativewindow, NULL);
    assert(surface != EGL_NO_SURFACE);

    // connect the context to the surface
    result = eglMakeCurrent(display, surface, surface, context);
    assert(EGL_FALSE != result);

    // Set background color and clear buffers
    glClearColor(0.15f, 0.25f, 0.35f, 1.0f);

    // Enable back face culling.
    glEnable(GL_CULL_FACE);

    glMatrixMode(GL_MODELVIEW);
#else
    // Create window
    WNDCLASS wc = {0};
    wc.style = CS_OWNDC;    // CS_HREDRAW | CS_VREDRAW | CS_OWNDC
    wc.lpfnWndProc = WinProc;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.lpszClassName = L"NesBrowserWindow";
    RegisterClass(&wc);
    auto w = GetSystemMetrics(SM_CXSCREEN);
    auto h = GetSystemMetrics(SM_CYSCREEN);
    w = REAL_SCREEN_W;
    h = REAL_SCREEN_H;
    auto hWnd = CreateWindow(L"NesBrowserWindow",
                             L"Nes Browser",
                             WS_POPUP | WS_VISIBLE,
                             0, 0, w, h,
                             nullptr, nullptr, nullptr, nullptr);

    // Initialize OpenGL
    GLuint PixelFormat;
    static PIXELFORMATDESCRIPTOR pfd =  // pfd Tells Windows How We Want Things To Be
    {
        sizeof(PIXELFORMATDESCRIPTOR),  // Size Of This Pixel Format Descriptor
        1,                              // Version Number
        PFD_DRAW_TO_WINDOW |            // Format Must Support Window
        PFD_SUPPORT_OPENGL |            // Format Must Support OpenGL
        PFD_DOUBLEBUFFER,               // Must Support Double Buffering
        PFD_TYPE_RGBA,                  // Request An RGBA Format
        32,                             // Select Our Color Depth
        0, 0, 0, 0, 0, 0,               // Color Bits Ignored
        0,                              // No Alpha Buffer
        0,                              // Shift Bit Ignored
        0,                              // No Accumulation Buffer
        0, 0, 0, 0,                     // Accumulation Bits Ignored
        16,                             // 16Bit Z-Buffer (Depth Buffer)
        0,                              // No Stencil Buffer
        0,                              // No Auxiliary Buffer
        PFD_MAIN_PLANE,                 // Main Drawing Layer
        0,                              // Reserved
        0, 0, 0                         // Layer Masks Ignored
    };
    hDC = GetDC(hWnd);
    PixelFormat = ChoosePixelFormat(hDC, &pfd);
    SetPixelFormat(hDC, PixelFormat, &pfd);
    hRC = wglCreateContext(hDC);
    wglMakeCurrent(hDC, hRC);
    ShowWindow(hWnd, SW_SHOW);  // Show The Window
    SetForegroundWindow(hWnd);  // Slightly Higher Priority
    SetFocus(hWnd);             // Sets Keyboard Focus To The Window
#endif

    printf("Initialized\n");
    
    while (true)
    {
        glClear(GL_COLOR_BUFFER_BIT);
        eglSwapBuffers(display, surface);
    }
    
    return 0;

    loadResources();

    // Setup states
    glDisable(GL_CULL_FACE);
    glDisable(GL_BLEND);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_ALPHA_TEST);
    glEnable(GL_TEXTURE_2D);
    glViewport(0, 0, REAL_SCREEN_W, REAL_SCREEN_H);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    //glOrtho(0, SCREEN_W, SCREEN_H, 0, -999, 999);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();

    // Main loop
#if defined(__GNUC__)
    while (!bDone)
    {
        mainLoopMutex.lock();
        if (update())
        {
            draw();
        }
        mainLoopMutex.unlock();
    }
#else
    MSG msg = {0};
    while (true)
    {
        if (PeekMessage(&msg, 0, 0, 0, PM_REMOVE))
        {
            TranslateMessage(&msg);
            DispatchMessage(&msg);

            if (msg.message == WM_QUIT)
            {
                break;
            }
        }

        mainLoopMutex.lock();
        if (update())
        {
            draw();
        }
        mainLoopMutex.unlock();
    }
#endif
    bDone = true;

    // Save recents
    std::ofstream out("recents.txt");
    if (!out.fail())
    {
        for (auto &filename : recentFilenames)
        {
            out << filename << std::endl;
        }
        out.close();
    }

    if (pTextureLoader) pTextureLoader->join();

    return 0;
}
