#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
    #ifndef NOMINMAX
        #define NOMINMAX
    #endif
    #include <windows.h>
    #include <conio.h>
#else
    #include <sys/ioctl.h>
    #include <termios.h>
    #include <unistd.h>
#endif

namespace term {
    static std::atomic<bool> quitRequested{false};
    static void onSignal(int) { quitRequested = true; }

    #ifdef _WIN32
        static DWORD savedOutMode = 0, savedInMode = 0;

        static void init() {
            HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE), in = GetStdHandle(STD_INPUT_HANDLE);
            GetConsoleMode(out, &savedOutMode);
            SetConsoleMode(out, savedOutMode | ENABLE_VIRTUAL_TERMINAL_PROCESSING | ENABLE_PROCESSED_OUTPUT);
            GetConsoleMode(in, &savedInMode);
            SetConsoleOutputCP(CP_UTF8);
        }
        static void restore() {
            SetConsoleMode(GetStdHandle(STD_OUTPUT_HANDLE), savedOutMode);
            SetConsoleMode(GetStdHandle(STD_INPUT_HANDLE), savedInMode);
        }
        static void size(int& cols, int& rows) {
            CONSOLE_SCREEN_BUFFER_INFO info;
            if (GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &info)) {
                cols = info.srWindow.Right - info.srWindow.Left + 1;
                rows = info.srWindow.Bottom - info.srWindow.Top + 1;
            } else {
                cols = 175; rows = 40;
            }
        }
        static double cellAspect() {
            CONSOLE_FONT_INFOEX font{};
            font.cbSize = sizeof font;
            if (GetCurrentConsoleFontEx(GetStdHandle(STD_OUTPUT_HANDLE), FALSE, &font) && font.dwFontSize.X > 0)
                return (double)font.dwFontSize.Y / font.dwFontSize.X;
            return 0;
        }
        static bool quitKeyPressed() {
            while (_kbhit()) {
                int key = _getch();
                if (key == 'q' || key == 'Q' || key == 27) return true;
            }
            return false;
        }
    #else
        static termios savedTermios;
        static bool termiosSaved = false;

        static void init() {
            if (tcgetattr(STDIN_FILENO, &savedTermios) == 0) {
                termiosSaved = true;
                termios raw = savedTermios;
                raw.c_lflag &= ~(ICANON | ECHO);
                raw.c_cc[VMIN] = 0;
                raw.c_cc[VTIME] = 0;
                tcsetattr(STDIN_FILENO, TCSANOW, &raw);
            }
        }
        static void restore() {
            if (termiosSaved) tcsetattr(STDIN_FILENO, TCSANOW, &savedTermios);
        }
        static void size(int& cols, int& rows) {
            winsize ws{};
            if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_col > 0) {
                cols = ws.ws_col; rows = ws.ws_row;
            } else {
                cols = 175; rows = 40;
            }
        }
        static double cellAspect() {
            winsize ws{};
            if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_xpixel > 0 && ws.ws_ypixel > 0 && ws.ws_col > 0 && ws.ws_row > 0)
                return ((double)ws.ws_ypixel / ws.ws_row) / ((double)ws.ws_xpixel / ws.ws_col);
            return 0;
        }
        static bool quitKeyPressed() {
            char key;
            while (read(STDIN_FILENO, &key, 1) == 1)
                if (key == 'q' || key == 'Q' || key == 27) return true;
            return false;
        }
    #endif

    static void write(const std::string& s) {
        fwrite(s.data(), 1, s.size(), stdout);
        fflush(stdout);
    }

    static const char* const ENTER_SCREEN = "\x1b[?1049h\x1b[?25l\x1b[40m\x1b[2J"; 
    static const char* const LEAVE_SCREEN = "\x1b[0m\x1b[?25h\x1b[?1049l";         
    static const char* const CLEAR        = "\x1b[2J";
    static const char* const HOME         = "\x1b[H";
} 

static const double PI = 3.14159265358979323846;

static inline double clamp01(double x) { return x < 0 ? 0 : x > 1 ? 1 : x; }
static inline double clampSigned(double x) { return std::max(-1.0, std::min(1.0, x)); }
static inline double lerp(double a, double b, double t) { return a + (b - a) * t; }

static inline double smoothstep(double e0, double e1, double x) {
    double t = clamp01((x - e0) / (e1 - e0));
    return t * t * (3 - 2 * t);
}

static uint32_t rngState = 1337;
static double random01() {
    rngState = rngState * 1664525u + 1013904223u;
    return rngState / 4294967296.0;
}

static double hash3(int x, int y, int z) {
    uint32_t h = (uint32_t)x * 374761393u + (uint32_t)y * 668265263u + (uint32_t)z * 2147483647u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return (h ^ (h >> 16)) / 4294967296.0;
}

static double valueNoise(double x, double y, double z) {
    int xi = (int)std::floor(x), yi = (int)std::floor(y), zi = (int)std::floor(z);
    double fx = x - xi, fy = y - yi, fz = z - zi;
    double u = fx * fx * (3 - 2 * fx), v = fy * fy * (3 - 2 * fy), w = fz * fz * (3 - 2 * fz);
    auto corner = [&](int dx, int dy, int dz) { return hash3(xi + dx, yi + dy, zi + dz); };
    double x00 = lerp(corner(0, 0, 0), corner(1, 0, 0), u), x10 = lerp(corner(0, 1, 0), corner(1, 1, 0), u);
    double x01 = lerp(corner(0, 0, 1), corner(1, 0, 1), u), x11 = lerp(corner(0, 1, 1), corner(1, 1, 1), u);
    return lerp(lerp(x00, x10, v), lerp(x01, x11, v), w);
}

static double fractalNoise(double x, double y, double z) {
    return 0.55 * valueNoise(x, y, z)
         + 0.3  * valueNoise(x * 2.03, y * 2.03, z * 2.03)
         + 0.15 * valueNoise(x * 4.1, y * 4.1, z * 4.1);
}

struct Rgb { double r, g, b; };
struct PaletteStop { double level, r, g, b; };

static const std::vector<PaletteStop> SUN_PALETTE = {
    {0, 40, 14, 10}, {0.3, 80, 34, 22}, {0.45, 108, 58, 36}, {0.6, 128, 98, 62},
    {0.74, 138, 130, 84}, {0.86, 145, 158, 118}, {1, 130, 175, 225}};
static const std::vector<PaletteStop> MOON_PALETTE = {
    {0, 22, 16, 70}, {0.3, 38, 24, 118}, {0.45, 42, 40, 158}, {0.6, 36, 78, 190},
    {0.75, 45, 105, 220}, {0.9, 62, 130, 240}, {1, 100, 160, 250}};

static Rgb samplePalette(const std::vector<PaletteStop>& palette, double level) {
    size_t i = 1;
    while (i < palette.size() - 1 && level > palette[i].level) i++;
    const PaletteStop& lo = palette[i - 1];
    const PaletteStop& hi = palette[i];
    double t = clamp01((level - lo.level) / (hi.level - lo.level));
    return {lerp(lo.r, hi.r, t), lerp(lo.g, hi.g, t), lerp(lo.b, hi.b, t)};
}

static const std::string RAMP = " .'`^\",:;Il!i><~+_-?][}{1)(|\\/tfjrxnuvczXYUJCLQ0OZmwqpdbkhao*#MW&8%B@$";

static const double CELL_H = 20;
static double CELL_W = 8;
static double aspectOverride = 0;   

static const double TERMINAL_GAIN = 1.45;

static int COLS = 175, ROWS = 40, CELLS = COLS * ROWS;
static double SCENE_W = COLS * CELL_W, SCENE_H = ROWS * CELL_H;

static std::vector<char>  cellChar;
static std::vector<float> cellColor;       
static std::vector<float> cellBodyLight;    
static std::vector<float> cellIsSky;         
static std::vector<float> revealThreshold;   

static void updateCellWidth() {
    double aspect = aspectOverride > 0 ? aspectOverride : term::cellAspect();
    if (!(aspect >= 1.4 && aspect <= 3.2)) aspect = 2.1;   
    CELL_W = CELL_H / aspect;
}

static void resizeGrid(int cols, int rows) {
    COLS = std::max(20, cols);
    ROWS = std::max(10, rows);
    CELLS = COLS * ROWS;
    updateCellWidth();
    SCENE_W = COLS * CELL_W;
    SCENE_H = ROWS * CELL_H;

    cellChar.assign(CELLS, ' ');
    cellColor.assign(CELLS * 3, 0);
    cellBodyLight.assign(CELLS, 0);
    cellIsSky.assign(CELLS, 1);
    revealThreshold.assign(CELLS, 0);

    for (int i = 0; i < CELLS; i++) {
        int col = i % COLS, row = i / COLS;
        revealThreshold[i] = (float)(0.65 * valueNoise(col * 0.13, row * 0.9, 3) + 0.35 * hash3(col, row, 7));
    }
}

static inline void setCellColor(int cell, const float* rgb, double gain) {
    cellColor[cell * 3]     = (float)(rgb[0] * gain);
    cellColor[cell * 3 + 1] = (float)(rgb[1] * gain);
    cellColor[cell * 3 + 2] = (float)(rgb[2] * gain);
}

static const double INTRO = 0.97, MOVE = 4.0, MERGED = 2.76, HOLD = 1.5;
static const double CYCLE = MOVE + MERGED + MOVE + HOLD;

struct Layout {
    double sunX, moonX, y;
    double merge;      
    double sunSide;   
};

static Layout layoutAt(double t) {
    double side = -1;  
    double merge = 0, sunSide = -1;

    double sinceIntro = t - INTRO;
    if (sinceIntro >= 0) {
        double cycle = std::floor(sinceIntro / CYCLE);
        double w = sinceIntro - cycle * CYCLE;                  
        double from = ((long long)cycle % 2 == 0) ? -1 : 1;     
        double approachEnd = MOVE, splitStart = MOVE + MERGED;

        if (w < approachEnd)               side = from * (1 - smoothstep(0, MOVE, w));
        else if (w < splitStart)           side = 0;
        else if (w < splitStart + MOVE)    side = -from * smoothstep(0, MOVE, w - splitStart);
        else                               side = -from;

        merge = smoothstep(approachEnd - 1.95, approachEnd - 0.35, w)
              * (1 - smoothstep(splitStart - 1.0, splitStart + 1.2, w));
        sunSide = w < approachEnd + MERGED / 2 ? from : -from;
    }

    double offset = 0.25 * SCENE_W;
    return {SCENE_W / 2 + offset * side, SCENE_W / 2 - offset * side, SCENE_H * 0.51, merge, sunSide};
}

static const int STAR_COUNT = 1100;
static const double STAR_SPREAD = 0.95, STAR_SPEED = 0.012;

struct Star {
    float x, y;                       
    float depth;                       
    float brightness;
    float twinkleSpeed, twinklePhase;
    float color[3];
};
static Star stars[STAR_COUNT];

static void spawnStar(Star& s, double depth) {
    s.x = (float)(random01() * 2 - 1);
    s.y = (float)(random01() * 2 - 1);
    s.depth = (float)depth;
    s.brightness = (float)(0.1 + 0.85 * std::pow(random01(), 4.5)); 
    s.twinkleSpeed = (float)(1.5 + random01() * 4);
    s.twinklePhase = (float)(random01() * 6.283);

    static const float GREY[3] = {0.36f, 0.36f, 0.42f}, BLUE[3] = {0.3f, 0.33f, 0.48f}, WARM[3] = {0.4f, 0.37f, 0.36f};
    double pick = random01();
    const float* color = pick < 0.7 ? GREY : pick < 0.88 ? BLUE : WARM;
    std::memcpy(s.color, color, sizeof s.color);
}

static void moveStars(double dt) {
    for (Star& s : stars) {
        s.depth -= (float)(STAR_SPEED * dt);
        if (s.depth < 0.08f) spawnStar(s, 1.0);
    }
}

static void drawStarFlare(const Star& s, int row, int col, double brightness) {
    int armLength = (int)std::floor(2 + 6 * (brightness - 0.5));
    for (int k = 1; k <= armLength; k++) {
        double armLight = (1 - (double)k / (armLength + 1)) * brightness;
        int rowStep = (k >> 1) + (k > 2 ? 1 : 0);  
        const int arms[4][2] = {{0, k}, {0, -k}, {rowStep, 0}, {-rowStep, 0}};
        const char armChar[4] = {'-', '-', '|', '|'};

        for (int j = 0; j < 4; j++) {
            int r = row + arms[j][0], c = col + arms[j][1];
            if (r < 0 || r >= ROWS || c < 0 || c >= COLS) continue;
            int cell = r * COLS + c;
            if (cellIsSky[cell] > 0.5f && cellBodyLight[cell] < armLight * 0.8) {
                cellChar[cell] = armLight > 0.18 ? armChar[j] : '.';
                setCellColor(cell, s.color, std::min(1.0, 0.25 + armLight));
            }
        }
    }
}

static void drawStars(double t, double fadeIn) {
    if (fadeIn <= 0) return;
    for (const Star& s : stars) {
        double depth = s.depth;
        int col = (int)std::floor((SCENE_W * 0.5 + (s.x * STAR_SPREAD / depth) * SCENE_H) / CELL_W);
        int row = (int)std::floor((SCENE_H * 0.5 + (s.y * STAR_SPREAD / depth) * SCENE_H) / CELL_H);
        double twinkle = 0.80 + 0.20 * std::sin(t * s.twinkleSpeed + s.twinklePhase);
        double alpha = smoothstep(1.0, 0.82, depth) * twinkle * fadeIn;   // new far stars fade in
        if (col < 0 || col >= COLS || row < 0 || row >= ROWS || alpha <= 0.04) continue;

        int cell = row * COLS + col;
        if (cellIsSky[cell] <= 0.5f) continue;
        double brightness = s.brightness * alpha * (1 + 0.05 / depth);
        if (!(brightness > cellBodyLight[cell] * 0.7)) continue;   // the body glow wins

        cellChar[cell] = brightness < 0.20 ? '.' : brightness < 0.42 ? '+' : '*';
        setCellColor(cell, s.color, std::min(1.0, std::max(0.0, 0.35 + 0.9 * brightness)));
        if (brightness > 0.55) {
            cellChar[cell] = brightness > 0.95 ? '@' : '*';
            drawStarFlare(s, row, col, brightness);
        }
    }
}

struct Meteor { double x, y, vx, vy, age, life; };
struct ScriptedMeteor { double time, x, y, vx, vy; };

static std::vector<Meteor> meteors;
static const ScriptedMeteor SCRIPTED_METEORS[] = {
    {1.37, 640, 322, 477, 270}, {2.77, 1080, 320, 372, 291}, {4.17, 365, 104, -345, 162},
    {8.27, 1065, 128, 408, 210}, {8.47, 540, 535, -276, 360}, {9.83, 260, 330, -330, 300}};
static const size_t SCRIPTED_COUNT = sizeof(SCRIPTED_METEORS) / sizeof(SCRIPTED_METEORS[0]);
static size_t nextScripted = 0;
static double nextRandomMeteorAt = 12;

static void spawnRandomMeteor() {
    bool goingRight = random01() < 0.5;
    double angle = (22 + random01() * 14) * PI / 180;
    double speed = 380 + random01() * 120;
    double x = goingRight ? random01() * SCENE_W * 0.7 : SCENE_W * 0.3 + random01() * SCENE_W * 0.7;
    double y = random01() * SCENE_H * 0.45;
    double vx = (goingRight ? 1 : -1) * std::cos(angle) * speed, vy = std::sin(angle) * speed;
    meteors.push_back({x, y, vx, vy, 0, 1.0 + random01() * 0.6});
}

static void updateMeteors(double t, double dt) {
    while (nextScripted < SCRIPTED_COUNT && t >= SCRIPTED_METEORS[nextScripted].time) {
        const ScriptedMeteor& m = SCRIPTED_METEORS[nextScripted++];
        meteors.push_back({m.x / 1400 * SCENE_W, m.y / 800 * SCENE_H, m.vx, m.vy, 0, 1.1});
    }
    if (t >= nextRandomMeteorAt) {
        spawnRandomMeteor();
        nextRandomMeteorAt = t + 1.1 + random01() * 1.6;
    }
    for (Meteor& m : meteors) {
        m.x += m.vx * dt;
        m.y += m.vy * dt;
        m.age += dt;
    }
    meteors.erase(std::remove_if(meteors.begin(), meteors.end(), [](const Meteor& m) { return m.age > m.life; }),
                  meteors.end());
}

static void drawMeteors() {
    const double TRAIL = 150, HEAD = 6, BODY = 95, STEP = 3;
    for (const Meteor& m : meteors) {
        double fade = smoothstep(0, 0.12, m.age) * (1 - smoothstep(m.life - 0.25, m.life, m.age));
        double speed = std::hypot(m.vx, m.vy), dirX = m.vx / speed, dirY = m.vy / speed;
        char bodyChar = ((m.vx > 0) == (m.vy > 0)) ? '\\' : '/';

        for (double back = TRAIL; back >= 0; back -= STEP) {
            int col = (int)std::floor((m.x - dirX * back) / CELL_W);
            int row = (int)std::floor((m.y - dirY * back) / CELL_H);
            if (col < 0 || col >= COLS || row < 0 || row >= ROWS) continue;
            int cell = row * COLS + col;
            double light = (1 - back / TRAIL) * fade;
            if (cellBodyLight[cell] > light * 0.8 || light < 0.05) continue;

            cellChar[cell] = back < HEAD ? '@' : back < BODY ? bodyChar : '.';
            double gain = back < HEAD ? 1 : 0.25 + 0.75 * light;
            cellColor[cell * 3]     = (float)(0.42 * gain);
            cellColor[cell * 3 + 1] = (float)(0.6 * gain);
            cellColor[cell * 3 + 2] = (float)(0.85 * gain);
        }
    }
}

static const double FIELD_EXP = 2.5;
static const double SUN_GAIN = 0.46, MOON_GAIN = 0.6;
static const double VERTICAL_SQUASH = 0.94;   

struct Body { double x, y, r; };

static double fieldOf(const Body& b, double x, double y) {
    return std::pow(std::max(std::hypot(x - b.x, y - b.y) / b.r, 1e-4), -FIELD_EXP);
}
static double rhoAt(double x, double y, const Body* bodies) {
    double field = 0;
    for (int i = 0; i < 2; i++) field += fieldOf(bodies[i], x, y);
    return std::pow(field, -1 / FIELD_EXP);
}

struct BodyFrame {
    Body bodies[2];                    
    double merge, sunSide, midX;
    double fusedRadius;                
    double balance;                    
    double reveal, sunWarm, sunRamp;   
    double moonLight[3];               
    double textureTime;
};
static const double SUN_LIGHT[3] = {0.55, -0.5, 0.67};   

static BodyFrame prepareBodyFrame(double t, const Layout& lay) {
    const double sunR = 0.295 * SCENE_H, moonR = 0.22 * SCENE_H;
    BodyFrame f;
    f.bodies[0] = {lay.sunX, lay.y, sunR};
    f.bodies[1] = {lay.moonX, lay.y, moonR};
    f.merge = lay.merge;
    f.sunSide = lay.sunSide;
    f.midX = (lay.sunX + lay.moonX) / 2;
    f.fusedRadius = std::pow(std::pow(sunR, FIELD_EXP) + std::pow(moonR, FIELD_EXP), 1 / FIELD_EXP);
    f.balance = std::pow(moonR / sunR, FIELD_EXP);
    f.reveal = 1.2 * smoothstep(0, 0.5, t);
    f.sunWarm = smoothstep(0.1, 0.7, t);
    f.sunRamp = lerp(0.8, 1, smoothstep(0.3, 2.2, t));

    double lx = (lay.sunX - lay.moonX) / SCENE_H, ly = 0, lz = 0.9;
    double len = std::sqrt(lx * lx + ly * ly + lz * lz);
    f.moonLight[0] = lx / len;
    f.moonLight[1] = ly / len;
    f.moonLight[2] = lz / len;
    f.textureTime = t * 0.06;
    return f;
}

static double sunWeight(const BodyFrame& f, double x, double sunField, double moonField) {

    double sunShare = std::pow(sunField * f.balance, 1.5), moonShare = std::pow(moonField, 1.5);
    double apart = sunShare / (sunShare + moonShare);

    double across = clampSigned((x - f.midX) * f.sunSide / f.fusedRadius);
    double threshold = lerp(-0.1, 1.4, f.merge);
    double fused = smoothstep(threshold - 0.35, threshold + 0.35, across);
    return lerp(apart, fused, smoothstep(0, 0.3, f.merge));
}

static double surfaceLight(const BodyFrame& f, double x, double y, double rho, double sunW, double moonW) {
    const Body& sun = f.bodies[0];
    const Body& moon = f.bodies[1];

    const double e = 1.5;
    double gx = rhoAt(x + e, y, f.bodies) - rhoAt(x - e, y, f.bodies);
    double gy = rhoAt(x, y + e, f.bodies) - rhoAt(x, y - e, f.bodies);
    double gLen = std::hypot(gx, gy);
    if (gLen == 0) gLen = 1;
    double nx = rho * gx / gLen, ny = rho * gy / gLen, nz = std::sqrt(std::max(0.0, 1 - rho * rho));

    double sunNoise = fractalNoise((x - sun.x) / sun.r * 3.4 + 11, (y - sun.y) / sun.r * 11, f.textureTime);
    double moonNoise = fractalNoise((x - moon.x) / moon.r * 3.2 + 50, (y - moon.y) / moon.r * 10, f.textureTime + 9);
    double sunTex = clampSigned((sunNoise - 0.5) / 0.17);
    double moonTex = clampSigned((moonNoise - 0.5) / 0.17);

    double ring = std::exp(-std::pow((rho - 0.8) / 0.22, 2));
    double sunKey = std::max(0.0, nx * SUN_LIGHT[0] + ny * SUN_LIGHT[1] + nz * SUN_LIGHT[2]);
    double moonDiffuse = std::max(0.0, nx * f.moonLight[0] + ny * f.moonLight[1] + nz * f.moonLight[2]);

    double sunL = 0.64 + 0.04 * ring + 0.14 * std::pow(sunKey, 3) + 0.12 * sunTex;
    double moonL = 0.57 + 0.25 * moonDiffuse + 0.11 * moonTex;
    double fusedL = lerp(0.95, 0.5, smoothstep(0.4, 1.0, rho)) + 0.06 * moonTex;   

    double light = lerp(sunW * sunL + moonW * moonL, fusedL, f.merge * f.merge);
    return lerp(light, 0.36 + 0.06 * moonTex, smoothstep(0.9, 1, rho));         
}

static void drawBodies(double t, const Layout& lay) {
    const BodyFrame f = prepareBodyFrame(t, lay);
    const Body& sun = f.bodies[0];
    const Body& moon = f.bodies[1];

    // Only visit columns that can contain a body.
    double x0 = std::min(sun.x - sun.r * 1.45, moon.x - moon.r * 1.45);
    double x1 = std::max(sun.x + sun.r * 1.45, moon.x + moon.r * 1.45);
    int col0 = std::max(0, (int)std::floor(x0 / CELL_W));
    int col1 = std::min(COLS - 1, (int)std::ceil(x1 / CELL_W));

    for (int row = 0; row < ROWS; row++) {
        double y = lay.y + ((row + 0.5) * CELL_H - lay.y) * VERTICAL_SQUASH;
        for (int col = col0; col <= col1; col++) {
            double x = (col + 0.5) * CELL_W;
            int cell = row * COLS + col;

            double sunField = fieldOf(sun, x, y), moonField = fieldOf(moon, x, y);
            double rho = std::pow(sunField + moonField, -1 / FIELD_EXP);
            double sunW = sunWeight(f, x, sunField, moonField), moonW = 1 - sunW;

            double halo = lerp(0.045, 0.0, moonW) + 0.06 * f.merge;
            if (rho > 1 + halo) continue;

            double light;
            if (rho < 1) {
                light = surfaceLight(f, x, y, rho, sunW, moonW);
            } else {
                if (rho > 1 + halo * (0.55 + 0.45 * revealThreshold[cell])) continue;
                light = 0.215 + 0.06 * hash3(col, row, 11);   
            }
            light = clamp01(light);

            if (revealThreshold[cell] - 0.3 * smoothstep(0.85, 1.05, rho) > f.reveal) continue;

            Rgb sunC = samplePalette(SUN_PALETTE, light), moonC = samplePalette(MOON_PALETTE, light);
            double grey = 0.299 * sunC.r + 0.587 * sunC.g + 0.114 * sunC.b;
            sunC = {lerp(grey, sunC.r, f.sunWarm), lerp(grey, sunC.g, f.sunWarm), lerp(grey, sunC.b, f.sunWarm)};
            double gain = lerp(SUN_GAIN * f.sunRamp, MOON_GAIN, moonW) * (0.5 + 0.5 * smoothstep(0.15, 0.9, light));

            cellColor[cell * 3]     = (float)((sunW * sunC.r + moonW * moonC.r) / 255 * gain);
            cellColor[cell * 3 + 1] = (float)((sunW * sunC.g + moonW * moonC.g) / 255 * gain);
            cellColor[cell * 3 + 2] = (float)((sunW * sunC.b + moonW * moonC.b) / 255 * gain);
            cellChar[cell] = RAMP[(size_t)std::lround(light * (RAMP.size() - 1))];
            cellBodyLight[cell] = (float)light;
            cellIsSky[cell] = rho < 1 ? 0.f : 1.f;
        }
    }
}

static std::string frameText;

static void present() {
    frameText.clear();
    frameText += term::HOME;
    int lastR = -1, lastG = -1, lastB = -1;
    char code[32];

    for (int row = 0; row < ROWS; row++) {
        for (int col = 0; col < COLS; col++) {
            int cell = row * COLS + col;
            char ch = cellChar[cell];
            if (ch != ' ') {
                int r = (int)std::min(255.0, cellColor[cell * 3] * 255 * TERMINAL_GAIN);
                int g = (int)std::min(255.0, cellColor[cell * 3 + 1] * 255 * TERMINAL_GAIN);
                int b = (int)std::min(255.0, cellColor[cell * 3 + 2] * 255 * TERMINAL_GAIN);
                if (r != lastR || g != lastG || b != lastB) {
                    snprintf(code, sizeof code, "\x1b[38;2;%d;%d;%dm", r, g, b);
                    frameText += code;
                    lastR = r; lastG = g; lastB = b;
                }
            }
            frameText += ch;
        }
        if (row + 1 < ROWS) frameText += "\r\n";   
    }
    term::write(frameText);
}

static void renderFrame(double t, double dt) {
    std::fill(cellChar.begin(), cellChar.end(), ' ');
    std::fill(cellColor.begin(), cellColor.end(), 0.f);
    std::fill(cellBodyLight.begin(), cellBodyLight.end(), 0.f);
    std::fill(cellIsSky.begin(), cellIsSky.end(), 1.f);

    moveStars(dt);
    updateMeteors(t, dt);

    drawBodies(t, layoutAt(t));
    drawStars(t, smoothstep(0.35, 1.0, t));
    drawMeteors();
    present();
}


static bool parseArgs(int argc, char** argv) {
    for (int i = 1; i < argc; i++) {
        if (std::strcmp(argv[i], "--aspect") == 0 && i + 1 < argc) {
            aspectOverride = std::atof(argv[++i]);
        } else {
            std::printf("usage: %s [--aspect H/W]   (character cell height/width, e.g. 2.1)\n", argv[0]);
            return false;
        }
    }
    return true;
}

int main(int argc, char** argv) {
    if (!parseArgs(argc, argv)) return 0;

    term::init();
    std::signal(SIGINT, term::onSignal);
#ifdef SIGTERM
    std::signal(SIGTERM, term::onSignal);
#endif

    for (Star& s : stars) spawnStar(s, 0.08 + 0.92 * random01());
    term::write(term::ENTER_SCREEN);

    int cols, rows;
    term::size(cols, rows);
    resizeGrid(cols, rows);

    using Clock = std::chrono::steady_clock;
    const auto frameDuration = std::chrono::microseconds(1000000 / 30);
    auto lastFrame = Clock::now();
    double t = 0;

    while (!term::quitRequested && !term::quitKeyPressed()) {
        auto now = Clock::now();
        double dt = std::min(0.1, std::chrono::duration<double>(now - lastFrame).count());
        lastFrame = now;
        t += dt;

        int newCols, newRows;
        term::size(newCols, newRows);
        if (newCols != COLS || newRows != ROWS) {
            resizeGrid(newCols, newRows);
            term::write(term::CLEAR);
        }

        renderFrame(t, dt);
        std::this_thread::sleep_until(now + frameDuration);
    }

    term::write(term::LEAVE_SCREEN);
    term::restore();
    return 0;
}
