#pragma once
#include <easyx.h>
#ifndef RGBA
#define RGBA(r, g, b, a) (((DWORD)(a) << 24) | RGB(r, g, b))
#endif
#include <vector>
#include <cmath>
#include <string>

const double PI = 3.14159265358979323846;
const int TABLE_WIDTH = 800;    // 球台宽度
const int TABLE_HEIGHT = 600;   // 球台高度
const int BALL_RADIUS = 12;     // 球半径
const double ELASTICITY = 0.85; // 弹性系数
const int CUSHION_WIDTH = 30;   // 台边宽度
const int POCKET_RADIUS = 25;   // 球洞半径
const int POCKET_INSET = 8;     // 球洞向球台内侧移动像素数
const double FRICTION = 0.98; // 摩擦系数
const double MIN_COLLISION_SPEED = 0.1;   // 最小碰撞速度

struct Vector2D {
    double x, y;

    Vector2D() : x(0), y(0) {}
    Vector2D(double x, double y) : x(x), y(y) {}

    Vector2D operator+(const Vector2D& other) const {
        return Vector2D(x + other.x, y + other.y);
    }

    Vector2D operator-(const Vector2D& other) const {
        return Vector2D(x - other.x, y - other.y);
    }

    Vector2D operator*(double scalar) const {
        return Vector2D(x * scalar, y * scalar);
    }

    double length() const {
        return sqrt(x * x + y * y);
    }

    Vector2D normalize() const {
        double len = length();
        if (len > 0.1) return Vector2D(x / len, y / len);
        return Vector2D(0, 0);
    }

    double dot(const Vector2D& other) const {
        return x * other.x + y * other.y;
    }

    Vector2D perpendicular() const {
        return Vector2D(-y, x);
    }
};

// Helper: compute pocket centers consistent with drawing
inline void computePocketCenters(Vector2D centers[6]) {
    double hx = CUSHION_WIDTH / 2.0;
    double hy = CUSHION_WIDTH / 2.0;
    centers[0] = Vector2D(hx + POCKET_INSET, hy + POCKET_INSET); // 左上
    centers[1] = Vector2D(TABLE_WIDTH - hx - POCKET_INSET, hy + POCKET_INSET); // 右上
    centers[2] = Vector2D(TABLE_WIDTH / 2.0, hy + POCKET_INSET); // 上中
    centers[3] = Vector2D(hx + POCKET_INSET, TABLE_HEIGHT - hy - POCKET_INSET); // 左下
    centers[4] = Vector2D(TABLE_WIDTH - hx - POCKET_INSET, TABLE_HEIGHT - hy - POCKET_INSET); // 右下
    centers[5] = Vector2D(TABLE_WIDTH / 2.0, TABLE_HEIGHT - hy - POCKET_INSET); // 下中
}

// Helper: check if position is over pocket area (used to skip cushion collision)
inline bool isOverPocketArea(const Vector2D& pos) {
    Vector2D centers[6];
    computePocketCenters(centers);
    double detectRadius = POCKET_RADIUS + BALL_RADIUS * 0.5; // slightly larger to avoid cushion bounce
    for (int i = 0; i < 6; ++i) {
        Vector2D d = pos - centers[i];
        if (d.length() < detectRadius) return true;
    }
    return false;
}

// 球类型
enum BallType {
    SOLID,    // 全色球
    STRIPED,  // 花色球
    CUE,      // 母球
    EIGHT     // 8号球
};

enum GameState {
    BREAK,
    ASSIGNMENT,
    PLAYER1_TURN,
    PLAYER2_TURN,
    FOUL_STATE,
    PLAYER1_WIN,
    PLAYER2_WIN
};

// 球类
class Ball {
public:
    Vector2D position;    // 位置
    Vector2D velocity;    // 速度
    COLORREF baseColor;   // 基础颜色
    COLORREF stripeColor; // 条纹颜色（如果是花色球）
    int number;           // 球号
    BallType type;        // 球类型
    bool isPocketed;      // 是否进袋
    double rotation;      // 旋转角度（用于渲染旋转效果）

    // pocket handling
    Vector2D pocketPosition; // 洞心位置，用于进袋动画
    double pocketTimer;      // 进袋计时
    bool toRemove;           // 标记为可从场景移除

    Ball(double x, double y, int number, BallType type, COLORREF color, COLORREF stripeColor = 0) {
        position = Vector2D(x, y);
        velocity = Vector2D(0, 0);
        this->number = number;
        this->type = type;
        this->baseColor = color;
        this->stripeColor = stripeColor;
        isPocketed = false;
        rotation = 0.0;
        pocketPosition = Vector2D(0,0);
        pocketTimer = 0.0;
        toRemove = false;
    }

    // 更新球的位置
    void update(double deltaTime) {
        if (isPocketed) {
            // 进袋动画：平滑移动到洞心，然后标记移除
            pocketTimer += deltaTime;
            double duration = 0.4; // 进袋动画时长
            double t = pocketTimer / duration;
            if (t > 1.0) t = 1.0;
            // 线性插值
            position.x = position.x * (1.0 - t) + pocketPosition.x * t;
            position.y = position.y * (1.0 - t) + pocketPosition.y * t;

            if (pocketTimer >= duration) {
                toRemove = true;
            }
            return;
        }

        // 应用摩擦力（基于帧时间以稳定表现）
        double damping = pow(FRICTION, deltaTime * 60.0); // 近似为每帧乘以 FRICTION
        velocity = velocity * damping;

        // 如果速度太小，停止移动
        if (velocity.length() < 0.1) {
            velocity = Vector2D(0, 0);
        }

        // 更新位置
        position = position + velocity * deltaTime;

        // 防止球因数值误差进入非播放区域
        if (position.x < 0) position.x = 0;
        if (position.y < 0) position.y = 0;
        if (position.x > TABLE_WIDTH) position.x = TABLE_WIDTH;
        if (position.y > TABLE_HEIGHT) position.y = TABLE_HEIGHT;
    }

    bool checkCollision(Ball& other) {
        if (isPocketed || other.isPocketed) return false;

        Vector2D distVec = position - other.position;
        double distance = distVec.length();

        // 增加碰撞检测范围，防止高速情况下穿透
        if (distance < 2.1 * BALL_RADIUS && distance > 0) {
            // 计算碰撞法向量
            Vector2D normal = distVec.normalize();

            // 计算相对速度
            Vector2D relativeVel = velocity - other.velocity;
            double velAlongNormal = relativeVel.dot(normal);

            // 修复：当球朝向彼此时才进行碰撞响应 (velAlongNormal应为负值)
            if (velAlongNormal >= 0) return false;

            // 修复：添加最小速度阈值，防止微小抖动产生碰撞
            if (fabs(velAlongNormal) < MIN_COLLISION_SPEED) return false;

            // 计算冲量大小 - 修复分母计算
            double impulseMagnitude = -(1 + ELASTICITY) * velAlongNormal;

            // 假设所有球质量相同，设为1.0
            double invMass1 = 1.0; // 可改为实际质量的倒数 1.0/mass
            double invMass2 = 1.0; // other球的质量倒数

            impulseMagnitude /= (invMass1 + invMass2);

            // 应用冲量
            Vector2D impulse = normal * impulseMagnitude;

            // 更新速度 - 使用质量倒数
            velocity = velocity + impulse * invMass1;
            other.velocity = other.velocity - impulse * invMass2;

            // 防止球重叠 - 优化分离方法
            double overlap = 2 * BALL_RADIUS - distance;
            if (overlap > 0) {
                Vector2D correction = normal * overlap * 0.5; // 均匀分离
                position = position + correction;
                other.position = other.position - correction;
            }

            return true;
        }
        return false;
    }

    // 检测与边界的碰撞
    void checkBoundaryCollision() {
        if (isPocketed) return;

        // 如果球位于球洞区域，跳过边界碰撞，使球能顺利进入洞中
        if (isOverPocketArea(position)) return;

        // 左右边界（考虑台边宽度）
        if (position.x - BALL_RADIUS < CUSHION_WIDTH) {
            position.x = CUSHION_WIDTH + BALL_RADIUS;
            velocity.x = -velocity.x * ELASTICITY;
        }
        else if (position.x + BALL_RADIUS > TABLE_WIDTH - CUSHION_WIDTH) {
            position.x = TABLE_WIDTH - CUSHION_WIDTH - BALL_RADIUS;
            velocity.x = -velocity.x * ELASTICITY;
        }

        // 上下边界
        if (position.y - BALL_RADIUS < CUSHION_WIDTH) {
            position.y = CUSHION_WIDTH + BALL_RADIUS;
            velocity.y = -velocity.y * ELASTICITY;
        }
        else if (position.y + BALL_RADIUS > TABLE_HEIGHT - CUSHION_WIDTH) {
            position.y = TABLE_HEIGHT - CUSHION_WIDTH - BALL_RADIUS;
            velocity.y = -velocity.y * ELASTICITY;
        }
    }

    // 绘制球（高级渲染）
    void draw() {
        if (isPocketed) return;
        int x = (int)position.x;
        int y = (int)position.y;
        int r = BALL_RADIUS;

        // 1. 绘制阴影
        drawShadow(x, y, r);

        // 2. 绘制球体渐变
        drawBallGradient(x, y, r);

        // 3. 绘制条纹（如果是花色球）
        if (type == STRIPED && number != 8) {
            drawStripe(x, y, r);
        }

        // 4. 绘制号码（加大字号）
        drawNumber(x, y, r);
    }

private:
    // 绘制球阴影
    void drawShadow(int x, int y, int r) {
        // 使用半透明椭圆作为阴影
        setfillcolor(RGBA(30, 30, 30, 100));
        solidellipse(x - r * 0.7, y + r * 0.8 - 2, x + r * 0.7, y + r * 0.8 + 3);
    }

    // 绘制球体渐变
    void drawBallGradient(int x, int y, int r) {
        setfillcolor(baseColor);
        solidcircle(x, y, r);
    }

    // 绘制条纹（花色球）
    void drawStripe(int x, int y, int r) {
        setlinecolor(stripeColor);
        setlinestyle(PS_SOLID, 3);

        // 计算条纹位置（根据旋转）
        double angle = fmod(rotation, 360) * PI / 180.0;
        Vector2D dir(cos(angle), sin(angle));
        Vector2D perp = dir.perpendicular();

        int x1 = (int)(x + perp.x * r * 0.5);
        int y1 = (int)(y + perp.y * r * 0.5);
        int x2 = (int)(x - perp.x * r * 0.5);
        int y2 = (int)(y - perp.y * r * 0.5);

        line(x1, y1, x2, y2);
    }

    // 绘制号码
    void drawNumber(int x, int y, int r) {
        if (number == 0) return; // 母球无号码

        TCHAR numText[3];
        _stprintf_s(numText, _T("%d"), number);

        // 背景圆（白色）
// 背景圆（白色）
        int bgRadius = (int)(r * 0.5); // 增大背景圆
        setfillcolor(WHITE);
        solidcircle(x, y, bgRadius);

        // 边框
        setlinecolor(BLACK);
        circle(x, y, bgRadius);

        // 号码文字
        // 始终使用黑色字体以便在白色背景上清晰可见
        settextcolor(BLACK);

        // 设置文本样式（增大字号）
        int fontSize = max(10, (int)(r * 0.9));
        settextstyle(fontSize, 0, _T("Arial"));

        // 使用 EasyX 提供的文本测量函数，避免直接访问 GDI DC
        int tw = textwidth(numText);
        int th = textheight(numText);
        outtextxy(x - tw / 2, y - th / 2, numText);
    }

    // 绘制高光（已禁用）
    void drawHighlight(int x, int y, int r) {
        // 已移除高光渲染
    }
};

// 球洞类
class Pocket {
public:
    Vector2D position;
    double radius;

    Pocket(double x, double y, double radius) {
        position = Vector2D(x, y);
        this->radius = radius;
    }

    // 绘制球洞
    void draw() {
        int x = (int)position.x;
        int y = (int)position.y;
        int r = (int)radius;

        // 1. 球洞阴影
        setfillcolor(RGBA(0, 0, 0, 120));
        solidcircle(x, y, r + 5);

        // 2. 球洞主体
        for (int i = 0; i < 5; i++) {
            int alpha = 220 - i * 40;
            int gray = 40 + i * 10;
            setfillcolor(RGBA(gray, gray, gray, alpha));
            solidcircle(x, y, r - i * 2);
        }

        // 3. 内部高光
        setfillcolor(RGBA(100, 100, 100, 150));
        solidcircle(x - 3, y - 3, r / 2);

        // 4. 洞内细节
        setlinecolor(RGBA(60, 60, 60, 200));
        setlinestyle(PS_SOLID, 1);
        for (int i = 0; i < 8; i++) {
            double angle = i * PI / 4;
            int x1 = (int)(x + cos(angle) * r * 0.2);
            int y1 = (int)(y + sin(angle) * r * 0.2);
            int x2 = (int)(x + cos(angle) * r * 0.8);
            int y2 = (int)(y + sin(angle) * r * 0.8);
            line(x1, y1, x2, y2);
        }
    }

    // 检测球是否进袋
    bool checkPocket(Ball& ball) {
        if (ball.isPocketed) return false; // already pocketed

        Vector2D distVec = ball.position - position;
        double distance = distVec.length();

        // 判定：球心进入洞心半径内即视为进袋
        if (distance < radius) {
            // 开始进袋：将球速度置零，记录洞心用于动画
            ball.isPocketed = true;
            ball.velocity = Vector2D(0,0);
            ball.pocketPosition = position;
            ball.pocketTimer = 0.0;
            return true;
        }
        return false;
    }
};

// 球台类
class BilliardTable {
public:
    void draw() {
        drawBackground();
        drawCushions();
        drawPockets();
    }

private:
    // 绘制台呢背景
    void drawBackground() {
        // 木质底座
        setfillcolor(RGB(139, 90, 43)); // 深棕色
        solidrectangle(0, 0, TABLE_WIDTH, TABLE_HEIGHT);

        // 台呢
        setfillcolor(RGB(15, 100, 15)); // 深绿色
        solidrectangle(CUSHION_WIDTH, CUSHION_WIDTH, TABLE_WIDTH - CUSHION_WIDTH, TABLE_HEIGHT - CUSHION_WIDTH);

        // 台呢纹理
        setlinecolor(RGBA(20, 120, 20, 100));
        setlinestyle(PS_SOLID, 1);

        for (int i = CUSHION_WIDTH + 5; i < TABLE_HEIGHT - CUSHION_WIDTH; i += 8) {
            line(CUSHION_WIDTH, i, TABLE_WIDTH - CUSHION_WIDTH, i);
        }

        for (int i = CUSHION_WIDTH + 5; i < TABLE_WIDTH - CUSHION_WIDTH; i += 8) {
            line(i, CUSHION_WIDTH, i, TABLE_HEIGHT - CUSHION_WIDTH);
        }
    }

    // 绘制台边缓冲垫
    void drawCushions() {
        // 上边
        drawCushion(CUSHION_WIDTH, CUSHION_WIDTH / 2, TABLE_WIDTH - CUSHION_WIDTH, CUSHION_WIDTH / 2, true);
        // 下边
        drawCushion(CUSHION_WIDTH, TABLE_HEIGHT - CUSHION_WIDTH / 2, TABLE_WIDTH - CUSHION_WIDTH, TABLE_HEIGHT - CUSHION_WIDTH / 2, true);
        // 左边
        drawCushion(CUSHION_WIDTH / 2, CUSHION_WIDTH, CUSHION_WIDTH / 2, TABLE_HEIGHT - CUSHION_WIDTH, false);
        // 右边
        drawCushion(TABLE_WIDTH - CUSHION_WIDTH / 2, CUSHION_WIDTH, TABLE_WIDTH - CUSHION_WIDTH / 2, TABLE_HEIGHT - CUSHION_WIDTH, false);
    }

    // 绘制单个缓冲垫
    void drawCushion(int x1, int y1, int x2, int y2, bool horizontal) {
        int width = horizontal ? CUSHION_WIDTH : CUSHION_WIDTH / 2;
        int height = horizontal ? CUSHION_WIDTH / 2 : CUSHION_WIDTH;

        // 缓冲垫主体
        setfillcolor(RGB(180, 30, 30)); // 深红色
        solidrectangle(x1 - (horizontal ? 0 : width),
            y1 - (horizontal ? height : 0),
            x2 + (horizontal ? 0 : width),
            y2 + (horizontal ? height : 0));

        // 缓冲垫表面
        setfillcolor(RGB(220, 60, 60)); // 亮红色
        if (horizontal) {
            solidrectangle(x1, y1 - height / 2, x2, y1);
        }
        else {
            solidrectangle(x1 - width / 2, y1, x1, y2);
        }

        // 缝线
        setlinecolor(RGB(255, 255, 255));
        setlinestyle(PS_DOT, 1);
        if (horizontal) {
            line(x1, y1 - height / 3, x2, y1 - height / 3);
        }
        else {
            line(x1 - width / 3, y1, x1 - width / 3, y2);
        }
    }

    // 绘制球洞
    void drawPockets() {
        // 计算基于缓冲垫中心的位置（与绘制的台面对齐），向内偏移 POCKET_INSET
        double hx = CUSHION_WIDTH / 2.0;
        double hy = CUSHION_WIDTH / 2.0;

        Vector2D pocketCenters[6] = {
            Vector2D(hx + POCKET_INSET, hy + POCKET_INSET), // 左上
            Vector2D(TABLE_WIDTH - hx - POCKET_INSET, hy + POCKET_INSET), // 右上
            Vector2D(TABLE_WIDTH / 2.0, hy + POCKET_INSET), // 上中
            Vector2D(hx + POCKET_INSET, TABLE_HEIGHT - hy - POCKET_INSET), // 左下
            Vector2D(TABLE_WIDTH - hx - POCKET_INSET, TABLE_HEIGHT - hy - POCKET_INSET), // 右下
            Vector2D(TABLE_WIDTH / 2.0, TABLE_HEIGHT - hy - POCKET_INSET) // 下中
        };

        for (int i = 0; i < 6; i++) {
            // 在台呢上擦除洞口区域，使该区域不算作台面（用木质底座色覆盖）
            setfillcolor(RGB(139, 90, 43));
            solidcircle((int)pocketCenters[i].x, (int)pocketCenters[i].y, POCKET_RADIUS + 6);

            Pocket pocket(pocketCenters[i].x, pocketCenters[i].y, POCKET_RADIUS);
            pocket.draw();
        }
    }
};

// 瞄准指示器
class AimingIndicator {
private:
    Vector2D cueBallPos;
    Vector2D aimDirection;
    double power;
    bool active;

public:
    AimingIndicator() : active(false), power(0) {}

    void update(const Vector2D& cuePos, const Vector2D& direction, double pwr, bool isActive) {
        cueBallPos = cuePos;
        aimDirection = direction.normalize();
        power = pwr;
        active = isActive;
    }

    void draw() {
        if (!active) return;

        int x = (int)cueBallPos.x;
        int y = (int)cueBallPos.y;
        int endX = (int)(x + aimDirection.x * 200);
        int endY = (int)(y + aimDirection.y * 200);

        // 1. 透明瞄准线
        setlinecolor(RGBA(255, 255, 0, 120)); // 半透明黄色
        setlinestyle(PS_SOLID, 2);
        line(x, y, endX, endY);

        // 2. 箭头
        drawArrow(endX, endY, aimDirection);

        // 3. 力量指示器
        drawPowerIndicator(x, y, power);

        // 4. 预测轨迹
        drawPredictionPath(x, y, aimDirection, power);
    }

private:
    void drawArrow(int x, int y, const Vector2D& direction) {
        Vector2D normDir = direction.normalize();
        Vector2D perp = normDir.perpendicular();

        int arrowSize = 10;
        int x1 = x - (int)(normDir.x * arrowSize) + (int)(perp.x * arrowSize / 2);
        int y1 = y - (int)(normDir.y * arrowSize) + (int)(perp.y * arrowSize / 2);
        int x2 = x - (int)(normDir.x * arrowSize) - (int)(perp.x * arrowSize / 2);
        int y2 = y - (int)(normDir.y * arrowSize) - (int)(perp.y * arrowSize / 2);

        setfillcolor(RGBA(255, 255, 0, 180));
        setlinecolor(RGBA(255, 200, 0, 200));
        POINT pts[3] = { {x, y}, {x1, y1}, {x2, y2} };
        fillpolygon(pts, 3);
    }

    void drawPowerIndicator(int x, int y, double power) {
        // 力量条背景
        setfillcolor(RGBA(50, 50, 50, 180));
        solidrectangle(x - 15, y - 80, x + 15, y - 20);

        // 力量条填充
        int fillHeight = (int)(power * 2.5);
        if (fillHeight > 50) fillHeight = 50;

        COLORREF powerColor;
        if (power < 7) powerColor = RGB(50, 200, 50);      // 低力量 - 绿色
        else if (power < 13) powerColor = RGB(200, 200, 50); // 中力量 - 黄色
        else powerColor = RGB(200, 50, 50);                 // 高力量 - 红色

        setfillcolor(RGBA(GetRValue(powerColor), GetGValue(powerColor), GetBValue(powerColor), 200));
        solidrectangle(x - 12, y - 77, x + 12, y - 77 + fillHeight);

        // 边框
        setlinecolor(WHITE);
        rectangle(x - 15, y - 80, x + 15, y - 20);

        // 标签
        settextcolor(WHITE);
        settextstyle(12, 0, _T("Arial"));
        TCHAR text[10];
        _stprintf_s(text, _T("%d"), (int)power);
        outtextxy(x - 5, y - 95, text);
    }

    void drawPredictionPath(int x, int y, const Vector2D& direction, double power) {
        // 简单预测球的轨迹
        Vector2D pos(x, y);
        Vector2D vel = direction * power * 5;

        setlinecolor(RGBA(255, 255, 255, 80));
        setlinestyle(PS_DOT, 1);

        for (int i = 0; i < 20; i++) {
            Vector2D newPos = pos + vel;

            // 简单边界检测
            if (newPos.x - BALL_RADIUS < CUSHION_WIDTH || newPos.x + BALL_RADIUS > TABLE_WIDTH - CUSHION_WIDTH) {
                vel.x = -vel.x * 0.8;
            }
            if (newPos.y - BALL_RADIUS < CUSHION_WIDTH || newPos.y + BALL_RADIUS > TABLE_HEIGHT - CUSHION_WIDTH) {
                vel.y = -vel.y * 0.8;
            }

            vel = vel * 0.9; // 模拟摩擦
            line((int)pos.x, (int)pos.y, (int)newPos.x, (int)newPos.y);
            pos = newPos;

            if (vel.length() < 1) break;
        }
    }
};