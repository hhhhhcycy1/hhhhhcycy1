#include <graphics.h>
#include <conio.h>
#include <time.h>
#include <string>
#include <vector>
#include <cmath>
#include "billiards.h"
#include <thread>
#include <chrono>
#include <algorithm>

using namespace std;

// 玩家结构体
struct Player {
    bool isCurrentPlayer;
    int assignedType; // 0 = 未分配, 1 = 全色, 2 = 花色
    int ballsLeft;
};

// 全局变量
Player player1, player2;
GameState gameState;
vector<Ball> balls;
vector<Pocket> pockets;
BilliardTable table;
AimingIndicator aimingIndicator;
clock_t messageStartTime;
double messageDuration = 2.0;
string message;
bool aiming = false;
Vector2D aimDirection;
double shotPower = 0;
bool gamePaused = false;

// 新增：记录一次击球过程（从发力到所有球停止/进袋动画完成）
bool shotInProgress = false;
bool pocketOccurredThisShot = false;

// gameplay constants
const double MAX_CUE_SPEED = 5000.0; // 最大白球速度（像素/秒）
const double SHOT_MULTIPLIER = 200.0; // 发力乘数

// 函数声明
void initGame();
void drawGameInfo();
void updateBalls(double deltaTime);
void checkCollisions();
void initBalls();
void initPockets();
void handleInput();
void showMessage(const char* msg);

// 主函数
int main() {
    // 初始化图形窗口
    initgraph(TABLE_WIDTH, TABLE_HEIGHT);
    setbkcolor(RGB(30, 120, 30)); // 绿色台面背景
    cleardevice();

    // 初始化随机数种子
    srand((unsigned)time(NULL));

    // 初始化球洞
    initPockets();

    // 初始化游戏
    initGame();

    // 记录上一帧时间
    clock_t lastTime = clock();

    const double targetFrameTime = 1.0 / 60.0; // 60 FPS

    // 游戏主循环
    while (true) {
        // 记录本帧开始时间并计算deltaTime
        clock_t frameStart = clock();
        double deltaTime = (double)(frameStart - lastTime) / CLOCKS_PER_SEC;
        lastTime = frameStart;

        // 处理输入
        handleInput();

        // 处理退出
        if (_kbhit()) {
            char key = _getch();
            if (key == 27) { // ESC键
                break;
            }
            else if (key == 'r' || key == 'R') {
                initGame(); // 重新开始游戏
            }
            else if (key == 'p' || key == 'P') {
                gamePaused = !gamePaused; // 暂停/继续游戏
            }
        }

        // 如果游戏暂停，跳过更新并保持帧率
        if (gamePaused) {
            std::this_thread::sleep_for(std::chrono::duration<double>(targetFrameTime));
            continue;
        }

        // 开始批量绘制以防止闪烁
        BeginBatchDraw();

        // 清屏
        cleardevice();

        // 绘制游戏元素
        table.draw();

        // 更新和绘制球
        updateBalls(deltaTime);
        checkCollisions();

        for (size_t i = 0; i < balls.size(); i++) {
            balls[i].draw();
        }

        // 绘制瞄准指示器
        if (aiming) {
            aimingIndicator.update(balls[0].position, aimDirection, shotPower, true);
            aimingIndicator.draw();
        }

        // 绘制游戏信息
        drawGameInfo();

        // 结束批量绘制并显示（减少闪烁）
        EndBatchDraw();

        // 控制帧率到60 FPS
        clock_t frameEnd = clock();
        double frameElapsed = (double)(frameEnd - frameStart) / CLOCKS_PER_SEC;
        double sleepTime = targetFrameTime - frameElapsed;
        if (sleepTime > 0) {
            std::this_thread::sleep_for(std::chrono::duration<double>(sleepTime));
        }
    }

    // 关闭图形窗口
    closegraph();
    return 0;
}

// 初始化游戏
void initGame() {
    // 初始化玩家
    player1.isCurrentPlayer = true;
    player1.assignedType = 0; // 0 = 未分配
    player1.ballsLeft = 7;

    player2.isCurrentPlayer = false;
    player2.assignedType = 0; // 0 = 未分配
    player2.ballsLeft = 7;

    // 移除开球阶段，直接进入玩家1回合
    gameState = PLAYER1_TURN;

    // 初始化球洞（与桌面绘制一致）
    initPockets();

    // 初始化球
    initBalls();

    // 重置瞄准状态
    aiming = false;
    shotPower = 0;
    gamePaused = false;

    // 重置击球跟踪
    shotInProgress = false;
    pocketOccurredThisShot = false;

    // 清除消息
    message = "";
}

// 初始化球
void initBalls() {
    balls.clear();

    // 将母球放在球台左侧安全区域，避开缓冲区
    double cueX = CUSHION_WIDTH + BALL_RADIUS + 40;
    double cueY = TABLE_HEIGHT / 2.0;
    balls.push_back(Ball(cueX, cueY, 0, CUE, WHITE));

    // 三角形排列（15球）
    const int rows = 5;
    const double spacing = BALL_RADIUS * 2 + 2; // 球间距（略大于直径以避免重叠）

    // 确定三角形起始位置（靠近右侧缓冲区并确保在台面内）
    double triangleBaseX = TABLE_WIDTH - CUSHION_WIDTH - BALL_RADIUS - (rows - 1) * spacing * 0.92;
    double triangleStartY = TABLE_HEIGHT / 2.0 - spacing * (rows - 1) / 2.0;

    // 颜色映射（1..15）
    vector<COLORREF> baseColors(16);
    vector<COLORREF> stripeColors(16, 0);
    baseColors[1] = RGB(255, 255, 0);   // 1
    baseColors[2] = RGB(0, 0, 255);     // 2
    baseColors[3] = RGB(255, 0, 0);     // 3
    baseColors[4] = RGB(128, 0, 128);   // 4
    baseColors[5] = RGB(255, 165, 0);   // 5
    baseColors[6] = RGB(0, 128, 0);     // 6
    baseColors[7] = RGB(165, 42, 42);   // 7
    baseColors[8] = RGB(0, 0, 0);       // 8
    // 9-15 are stripes: same base colors as 1-7, stripe color white
    for (int i = 9; i <= 15; ++i) {
        baseColors[i] = baseColors[i - 8];
        stripeColors[i] = WHITE;
    }

    int number = 1;
    for (int r = 0; r < rows; ++r) {
        for (int c = 0; c <= r; ++c) {
            if (number > 15) break;
            double x = triangleBaseX + r * spacing * 0.92;
            double y = triangleStartY + c * spacing + (spacing * (rows - 1 - r) / 2.0);

            BallType type = (number == 8) ? EIGHT : ((number <= 7) ? SOLID : STRIPED);
            COLORREF stripe = stripeColors[number];
            COLORREF base = baseColors[number];

            balls.push_back(Ball(x, y, number, type, base, stripe));
            ++number;
        }
    }
}

// 初始化球洞
void initPockets() {
    pockets.clear();

    double hx = CUSHION_WIDTH / 2.0;
    double hy = CUSHION_WIDTH / 2.0;

    pockets.push_back(Pocket(hx, hy, POCKET_RADIUS)); // 左上
    pockets.push_back(Pocket(TABLE_WIDTH - hx, hy, POCKET_RADIUS)); // 右上
    pockets.push_back(Pocket(TABLE_WIDTH / 2.0, hy, POCKET_RADIUS)); // 上中
    pockets.push_back(Pocket(hx, TABLE_HEIGHT - hy, POCKET_RADIUS)); // 左下
    pockets.push_back(Pocket(TABLE_WIDTH - hx, TABLE_HEIGHT - hy, POCKET_RADIUS)); // 右下
    pockets.push_back(Pocket(TABLE_WIDTH / 2.0, TABLE_HEIGHT - hy, POCKET_RADIUS)); // 下中
}

// 绘制游戏信息
void drawGameInfo() {
    // 设置文本样式
    settextstyle(18, 0, _T("Arial"));

    // 绘制玩家1信息
    COLORREF player1Color = player1.isCurrentPlayer ? RGB(255, 255, 0) : WHITE;
    settextcolor(player1Color);
    outtextxy(10, 10, _T("玩家 1"));

    // 绘制玩家2信息
    COLORREF player2Color = player2.isCurrentPlayer ? RGB(255, 255, 0) : WHITE;
    settextcolor(player2Color);
    outtextxy(TABLE_WIDTH - 100, 10, _T("玩家 2"));

    // 显示当前状态
    settextcolor(RGB(255, 200, 100));
    settextstyle(24, 0, _T("Arial"));

    TCHAR statusText[50];
    switch (gameState) {
    case BREAK:
        _stprintf_s(statusText, _T("开球阶段"));
        break;
    case ASSIGNMENT:
        _stprintf_s(statusText, _T("分配球组中..."));
        break;
    case PLAYER1_TURN:
        _stprintf_s(statusText, _T("玩家1回合"));
        break;
    case PLAYER2_TURN:
        _stprintf_s(statusText, _T("玩家2回合"));
        break;
    case FOUL_STATE:
        _stprintf_s(statusText, _T("犯规! 自由球"));
        break;
    case PLAYER1_WIN:
        _stprintf_s(statusText, _T("玩家1获胜!"));
        break;
    case PLAYER2_WIN:
        _stprintf_s(statusText, _T("玩家2获胜!"));
        break;
    default:
        _stprintf_s(statusText, _T("游戏进行中"));
    }
    outtextxy(TABLE_WIDTH / 2 - 120, TABLE_HEIGHT - 40, statusText);

    // 显示消息
    if (!message.empty()) {
        settextcolor(RGB(255, 100, 100));
        settextstyle(28, 0, _T("Arial"));

#ifdef _UNICODE
        std::wstring wmsg(message.begin(), message.end());
        int msgWidth = textwidth(wmsg.c_str());
        outtextxy(TABLE_WIDTH / 2 - msgWidth / 2, TABLE_HEIGHT / 2 - 20, wmsg.c_str());
#else
        int msgWidth = textwidth(message.c_str());
        outtextxy(TABLE_WIDTH / 2 - msgWidth / 2, TABLE_HEIGHT / 2 - 20, message.c_str());
#endif
    }

    // 结束游戏画面
    if (gameState == PLAYER1_WIN || gameState == PLAYER2_WIN) {
        setfillcolor(RGBA(0, 0, 0, 180));
        solidrectangle(0, 0, TABLE_WIDTH, TABLE_HEIGHT);

        settextcolor(WHITE);
        settextstyle(36, 0, _T("Arial"));
        outtextxy(TABLE_WIDTH / 2 - 150, TABLE_HEIGHT / 2 - 50,
            gameState == PLAYER1_WIN ? _T("玩家1获胜!") : _T("玩家2获胜!"));

        settextstyle(24, 0, _T("Arial"));
        outtextxy(TABLE_WIDTH / 2 - 120, TABLE_HEIGHT / 2 + 20, _T("按R键重新开始"));
    }

    // 暂停提示
    if (gamePaused) {
        setfillcolor(RGBA(0, 0, 0, 150));
        solidrectangle(TABLE_WIDTH / 2 - 100, TABLE_HEIGHT / 2 - 30, TABLE_WIDTH / 2 + 100, TABLE_HEIGHT / 2 + 30);
        settextcolor(WHITE);
        settextstyle(32, 0, _T("Arial"));
        outtextxy(TABLE_WIDTH / 2 - 50, TABLE_HEIGHT / 2 - 15, _T("暂停"));
    }
}

// 更新球的位置
void updateBalls(double deltaTime) {
    // 先检测是否进袋并更新位置
    for (size_t i = 0; i < balls.size(); i++) {
        // 检查是否进袋
        for (size_t j = 0; j < pockets.size(); j++) {
            if (!balls[i].isPocketed) {
                // 如果 pocket 在此帧发生且当前为一次击球过程，则记录
                if (pockets[j].checkPocket(balls[i])) {
                    if (shotInProgress) pocketOccurredThisShot = true;
                }
            }
        }

        // 更新球的位置
        balls[i].update(deltaTime);

        // 检查边界碰撞
        balls[i].checkBoundaryCollision();
    }

    // 移除已经完成进袋动画的球
    bool cueRespawn = false;
    // 记录是否母球被移除
    for (const Ball& b : balls) {
        if (b.toRemove && b.number == 0) {
            cueRespawn = true;
            break;
        }
    }

    // 构建新的球列表，保留未标记移除的球
    vector<Ball> newBalls;
    newBalls.reserve(balls.size());
    for (const Ball& b : balls) {
        if (!b.toRemove) {
            newBalls.push_back(b);
        }
    }

    // 如果母球被移除，重生一个母球并放在安全位置（确保它为 newBalls[0]）
    if (cueRespawn) {
        double cueX = CUSHION_WIDTH + BALL_RADIUS + 40;
        double cueY = TABLE_HEIGHT / 2.0;
        Ball cueBall(cueX, cueY, 0, CUE, WHITE);
        // 将母球放在最前面
        newBalls.insert(newBalls.begin(), cueBall);
    }

    // 如果母球没有被移除，确保母球仍然是 newBalls[0]
    // 若当前 newBalls 中没有母球，则插入一个新的母球
    bool hasCue = false;
    for (const Ball& b : newBalls) {
        if (b.number == 0) { hasCue = true; break; }
    }
    if (!hasCue) {
        double cueX = CUSHION_WIDTH + BALL_RADIUS + 40;
        double cueY = TABLE_HEIGHT / 2.0;
        Ball cueBall(cueX, cueY, 0, CUE, WHITE);
        newBalls.insert(newBalls.begin(), cueBall);
    }

    balls.swap(newBalls);

    // 如果当前处于一次击球中，检测是否所有球均已停止（包括进袋动画完成）
    if (shotInProgress) {
        bool anyMovingOrAnimating = false;
        for (const Ball& b : balls) {
            if (b.velocity.length() > 0.1) {
                anyMovingOrAnimating = true;
                break;
            }
            if (b.isPocketed && !b.toRemove) {
                // 仍在进袋动画中
                anyMovingOrAnimating = true;
                break;
            }
        }

        if (!anyMovingOrAnimating) {
            // 本次击球结束：若本杆没有进球，则换人（切换名称高亮）
            if (!pocketOccurredThisShot) {
                if (gameState == PLAYER1_TURN) {
                    gameState = PLAYER2_TURN;
                    player1.isCurrentPlayer = false;
                    player2.isCurrentPlayer = true;
                }
                else if (gameState == PLAYER2_TURN) {
                    gameState = PLAYER1_TURN;
                    player1.isCurrentPlayer = true;
                    player2.isCurrentPlayer = false;
                }
            }

            // 重置击球跟踪状态
            shotInProgress = false;
            pocketOccurredThisShot = false;
        }
    }
}

// 检查球之间的碰撞
void checkCollisions() {
    for (size_t i = 0; i < balls.size(); i++) {
        if (balls[i].isPocketed) continue;

        for (size_t j = i + 1; j < balls.size(); j++) {
            if (balls[j].isPocketed) continue;

            balls[i].checkCollision(balls[j]);
        }
    }
}

// 处理用户输入
void handleInput() {
    static bool leftMouseDown = false;
    static POINT lastMousePos;

    // 获取鼠标状态
    MOUSEMSG msg;
    while (MouseHit()) {
        msg = GetMouseMsg();

        switch (msg.uMsg) {
        case WM_LBUTTONDOWN:
            leftMouseDown = true;
            lastMousePos.x = msg.x;
            lastMousePos.y = msg.y;

            // 如果是瞄准状态，按下左键开始调整力量
            if (aiming && !balls[0].isPocketed && balls[0].velocity.length() < 0.1) {
                shotPower = 0;
            }
            break;

        case WM_LBUTTONUP:
            leftMouseDown = false;

            // 如果是瞄准状态并且正在调整力量，释放左键发射球
            if (aiming && shotPower > 0 && !balls[0].isPocketed && balls[0].velocity.length() < 0.1) {
                Vector2D dir = aimDirection.normalize();
                balls[0].velocity = dir * shotPower * SHOT_MULTIPLIER;
                // 限制白球最大速度
                double speed = balls[0].velocity.length();
                if (speed > MAX_CUE_SPEED) {
                    balls[0].velocity = balls[0].velocity.normalize() * MAX_CUE_SPEED;
                }

                aiming = false;
                shotPower = 0;

                // 标记一次新的击球开始，实际换人将在击球结束后判断
                shotInProgress = true;
                pocketOccurredThisShot = false;
            }
            break;

        case WM_MOUSEMOVE:
            if (aiming && leftMouseDown && balls[0].velocity.length() < 0.1) {
                // 调整力量
                shotPower += 0.5;
                if (shotPower > 20) shotPower = 20;
            }
            else if (!leftMouseDown && !balls[0].isPocketed && balls[0].velocity.length() < 0.1) {
                // 更新瞄准方向
                aimDirection = Vector2D(msg.x - balls[0].position.x, msg.y - balls[0].position.y);
                aiming = true;
            }
            break;
        }
    }
}

// 显示消息
void showMessage(const char* msg) {
    message = msg;
    messageStartTime = clock();
}