// ============================================================
// 文件名: auto.cpp
// 功能: Lyra (com.george.lyra) 骨骼锁头 + 子弹磁力追踪
//       配置由 /sdcard/hack.cfg 控制，与悬浮窗 APK 联动
// 编译: clang++ -std=c++17 -pthread -O2 auto.cpp -o pmagnet
// 用法: su -c "nohup ./pmagnet > /dev/null 2>&1 &"
// ============================================================
#include <iostream>
#include <vector>
#include <cmath>
#include <thread>
#include <chrono>
#include <fstream>
#include <string>
#include <dirent.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/uio.h>

// ==================== 用户配置（硬编码默认值） ====================
#define HEAD_BONE_INDEX 8          // 头部骨骼索引（Lyra 默认 8）
#define DEFAULT_AIM_UP_OFFSET 0.0f // 额外抬高（骨骼已精准，保持 0）
#define DEFAULT_FOV_ANGLE 60.0f    // 视野角度
#define SPEED_THRESHOLD 50.0f      // 子弹速度下限
// ===============================================================

// ==================== 全局功能开关（由配置文件覆盖） ====================
bool bMagnetEnabled = true;
bool bAimbotEnabled = true;
int AimBoneIndex = HEAD_BONE_INDEX;
float SmoothFactor = 1.0f;         // 暂未使用，保留扩展

// ==================== 偏移定义 ====================
namespace Offsets {
    // GWorld 绝对地址（你的 Dump 结果）
    constexpr uintptr_t GWorld = 0xb7e6538;

    constexpr uintptr_t UWorld_PersistentLevel = 0x30;
    constexpr uintptr_t ULevel_Actors_Data  = 0xA0;
    constexpr uintptr_t ULevel_Actors_Count = 0xA8;
    constexpr uintptr_t AActor_RootComponent = 0x1B8;
    constexpr uintptr_t AActor_MovementComponent = 0x168;
    constexpr uintptr_t USceneComponent_RelativeLocation = 0x140;
    constexpr uintptr_t USceneComponent_ComponentVelocity = 0x188;
    constexpr uintptr_t UMovementComponent_Velocity = 0xD0;
    constexpr uintptr_t UWorld_GameInstance = 0x190;
    constexpr uintptr_t UGameInstance_LocalPlayers = 0x38;
    constexpr uintptr_t ULocalPlayer_PlayerController = 0x30;
    constexpr uintptr_t APlayerController_AcknowledgedPawn = 0x330;
    constexpr uintptr_t APlayerController_ControlRotation = 0x440;
    constexpr uintptr_t ACharacter_Mesh = 0x310;
    constexpr uintptr_t USkeletalMeshComponent_BoneTransforms = 0x6E0; // 备选 0x6F0
}

// ==================== 基础数据结构 ====================
struct FVector { float X, Y, Z; };
struct FQuat { float X, Y, Z, W; };
struct FTransform { FQuat Rotation; FVector Translation; FVector Scale3D; };
struct FRotator { float Pitch, Yaw, Roll; };

inline FVector operator-(const FVector& a, const FVector& b) { return {a.X-b.X, a.Y-b.Y, a.Z-b.Z}; }
inline float Length(const FVector& v) { return sqrtf(v.X*v.X + v.Y*v.Y + v.Z*v.Z); }
inline FVector Normalize(const FVector& v) { float l=Length(v); if(l<1e-6f)return v; return {v.X/l, v.Y/l, v.Z/l}; }
inline float Dot(const FVector& a, const FVector& b) { return a.X*b.X + a.Y*b.Y + a.Z*b.Z; }

inline FVector GetForwardVector(const FRotator& rot) {
    float pitch = rot.Pitch * 3.14159265f / 180.0f;
    float yaw   = rot.Yaw   * 3.14159265f / 180.0f;
    return { cosf(pitch)*cosf(yaw), cosf(pitch)*sinf(yaw), sinf(pitch) };
}

// ==================== 内存读写（通过 /proc/pid/mem） ====================
int mem_fd = -1;
bool AttachProcess(pid_t pid) {
    char path[64]; sprintf(path, "/proc/%d/mem", pid);
    mem_fd = open(path, O_RDWR);
    return mem_fd != -1;
}

template<typename T>
T ReadMem(uintptr_t addr) {
    T val{};
    if (mem_fd == -1) return val;
    lseek(mem_fd, addr, SEEK_SET);
    read(mem_fd, &val, sizeof(T));
    return val;
}

template<typename T>
void WriteMem(uintptr_t addr, T val) {
    if (mem_fd == -1) return;
    lseek(mem_fd, addr, SEEK_SET);
    write(mem_fd, &val, sizeof(T));
}

// 获取 libUE4.so 基址（用于必要时，目前用绝对地址）
uintptr_t GetLibUE4Base(pid_t pid) {
    char path[64]; sprintf(path, "/proc/%d/maps", pid);
    std::ifstream maps(path);
    std::string line;
    while (std::getline(maps, line)) {
        if (line.find("libUE4.so") != std::string::npos || line.find("libUnreal.so") != std::string::npos) {
            size_t dash = line.find('-');
            if (dash != std::string::npos) {
                std::string addr_str = line.substr(0, dash);
                return std::stoull(addr_str, nullptr, 16);
            }
        }
    }
    return 0;
}

// ==================== 获取本地玩家相关 ====================
uintptr_t GetLocalPlayerController(uintptr_t World) {
    uintptr_t GI = ReadMem<uintptr_t>(World + Offsets::UWorld_GameInstance);
    if (!GI) return 0;
    uintptr_t LPtr = ReadMem<uintptr_t>(GI + Offsets::UGameInstance_LocalPlayers);
    if (!LPtr) return 0;
    uintptr_t LPlayer = ReadMem<uintptr_t>(LPtr);
    if (!LPlayer) return 0;
    return ReadMem<uintptr_t>(LPlayer + Offsets::ULocalPlayer_PlayerController);
}

uintptr_t GetLocalPawn(uintptr_t World) {
    uintptr_t PC = GetLocalPlayerController(World);
    if (!PC) return 0;
    return ReadMem<uintptr_t>(PC + Offsets::APlayerController_AcknowledgedPawn);
}

// ==================== 骨骼锁头（获取目标头部世界坐标） ====================
bool GetBoneHeadLocation(uintptr_t Actor, FVector& outHeadPos) {
    uintptr_t Mesh = ReadMem<uintptr_t>(Actor + Offsets::ACharacter_Mesh);
    if (!Mesh) return false;

    uintptr_t BoneArray = ReadMem<uintptr_t>(Mesh + Offsets::USkeletalMeshComponent_BoneTransforms);
    int32_t BoneCount = ReadMem<int32_t>(Mesh + Offsets::USkeletalMeshComponent_BoneTransforms + 0x8);
    if (!BoneArray || BoneCount <= AimBoneIndex) {
        // 备选偏移 0x6F0
        BoneArray = ReadMem<uintptr_t>(Mesh + 0x6F0);
        BoneCount = ReadMem<int32_t>(Mesh + 0x6F0 + 0x8);
        if (!BoneArray || BoneCount <= AimBoneIndex) return false;
    }

    FTransform BoneTransform = ReadMem<FTransform>(BoneArray + AimBoneIndex * 0x30);
    FVector BoneLocal = BoneTransform.Translation;

    uintptr_t MeshRoot = ReadMem<uintptr_t>(Mesh + Offsets::AActor_RootComponent);
    if (!MeshRoot) return false;
    FVector MeshWorld = ReadMem<FVector>(MeshRoot + Offsets::USceneComponent_RelativeLocation);
    outHeadPos = { MeshWorld.X + BoneLocal.X, MeshWorld.Y + BoneLocal.Y, MeshWorld.Z + BoneLocal.Z };
    return true;
}

// ==================== 加载配置文件 ====================
void LoadConfig() {
    std::ifstream cfg("/sdcard/hack.cfg");
    if (!cfg.is_open()) return;

    std::string line;
    while (std::getline(cfg, line)) {
        if (line == "magnet=1") bMagnetEnabled = true;
        else if (line == "magnet=0") bMagnetEnabled = false;
        else if (line == "aim=1") bAimbotEnabled = true;
        else if (line == "aim=0") bAimbotEnabled = false;
        else if (line.find("bone=") == 0) AimBoneIndex = std::stoi(line.substr(5));
        else if (line.find("smooth=") == 0) SmoothFactor = std::stof(line.substr(7));
    }
    cfg.close();
}

// ==================== 核心功能（每帧执行） ====================
void RunProjectileMagnet() {
    // 每帧加载配置（响应悬浮窗操作）
    LoadConfig();

    if (!bMagnetEnabled && !bAimbotEnabled) return;

    uintptr_t World = ReadMem<uintptr_t>(Offsets::GWorld);
    if (!World) {
        static bool warned = false;
        if (!warned) { std::cerr << "[-] GWorld=0\n"; warned = true; }
        return;
    }

    uintptr_t LocalPC = GetLocalPlayerController(World);
    if (!LocalPC) return;
    FRotator ControlRot = ReadMem<FRotator>(LocalPC + Offsets::APlayerController_ControlRotation);
    FVector Forward = GetForwardVector(ControlRot);

    uintptr_t LocalPawn = GetLocalPawn(World);
    FVector LocalPos{0,0,0};
    if (LocalPawn) {
        uintptr_t LRoot = ReadMem<uintptr_t>(LocalPawn + Offsets::AActor_RootComponent);
        if (LRoot) LocalPos = ReadMem<FVector>(LRoot + Offsets::USceneComponent_RelativeLocation);
    }

    uintptr_t Level = ReadMem<uintptr_t>(World + Offsets::UWorld_PersistentLevel);
    if (!Level) return;
    uintptr_t ActorsData = ReadMem<uintptr_t>(Level + Offsets::ULevel_Actors_Data);
    int32_t ActorCount = ReadMem<int32_t>(Level + Offsets::ULevel_Actors_Count);
    if (ActorsData == 0 || ActorCount <= 0 || ActorCount > 100000) {
        ActorsData = ReadMem<uintptr_t>(Level + 0xE0);
        ActorCount = ReadMem<int32_t>(Level + 0xE8);
        if (ActorCount <= 0 || ActorCount > 100000) return;
    }
    if (!ActorsData || ActorCount <= 0) return;

    // ---------- 寻找目标（最近 + 视野内） ----------
    uintptr_t TargetActor = 0;
    FVector TargetPos{0,0,0};
    float MinDist = 1e9f;
    float CosAngle = cosf(DEFAULT_FOV_ANGLE * 3.14159265f / 180.0f);

    for (int i = 0; i < ActorCount; ++i) {
        uintptr_t Actor = ReadMem<uintptr_t>(ActorsData + i * 8);
        if (!Actor || Actor == LocalPawn) continue;

        FVector HeadPos;
        bool hasBone = GetBoneHeadLocation(Actor, HeadPos);
        FVector Pos = hasBone ? HeadPos : ([](){ FVector p; return p; })(); // fallback 需单独处理
        if (!hasBone) {
            // 降级方案：使用 RootComponent + 抬高
            uintptr_t Root = ReadMem<uintptr_t>(Actor + Offsets::AActor_RootComponent);
            if (!Root) continue;
            Pos = ReadMem<FVector>(Root + Offsets::USceneComponent_RelativeLocation);
            Pos.Z += 100.0f;
        }

        FVector Delta = Pos - LocalPos;
        float dist = Length(Delta);
        if (dist < 100.0f || dist > 15000.0f) continue;
        FVector Dir = Normalize(Delta);
        float dot = Dot(Dir, Forward);
        if (dot < CosAngle) continue;
        if (dist < MinDist) {
            MinDist = dist;
            TargetActor = Actor;
            TargetPos = Pos;
        }
    }
    if (!TargetActor) return;

    // ---------- 修正子弹 ----------
    static int frame = 0;
    int modified = 0;

    for (int i = 0; i < ActorCount; ++i) {
        uintptr_t Actor = ReadMem<uintptr_t>(ActorsData + i * 8);
        if (!Actor || Actor == LocalPawn || Actor == TargetActor) continue;

        uintptr_t MovComp = ReadMem<uintptr_t>(Actor + Offsets::AActor_MovementComponent);
        uintptr_t Root = ReadMem<uintptr_t>(Actor + Offsets::AActor_RootComponent);
        if (!Root) continue;

        FVector Vel{0,0,0};
        if (MovComp) {
            Vel = ReadMem<FVector>(MovComp + Offsets::UMovementComponent_Velocity);
        } else {
            Vel = ReadMem<FVector>(Root + Offsets::USceneComponent_ComponentVelocity);
        }

        float speed = Length(Vel);
        if (speed < SPEED_THRESHOLD) continue;

        FVector BulletPos = ReadMem<FVector>(Root + Offsets::USceneComponent_RelativeLocation);
        FVector Dir = Normalize(TargetPos - BulletPos);
        FVector NewVel = {Dir.X * speed, Dir.Y * speed, Dir.Z * speed};

        if (MovComp) {
            WriteMem<FVector>(MovComp + Offsets::UMovementComponent_Velocity, NewVel);
            modified++;
        } else {
            WriteMem<FVector>(Root + Offsets::USceneComponent_ComponentVelocity, NewVel);
            modified++;
        }
    }

    if (++frame % 60 == 0) {
        std::cout << "[帧" << frame << "] 距离:" << (int)MinDist << "cm 修正:" << modified << "颗\n";
    }
}

// ==================== 主程序 ====================
int main() {
    std::cout << "Lyra 后台服务启动 (骨骼锁头 + 子弹追踪)\n";

    DIR* dir = opendir("/proc");
    struct dirent* entry;
    pid_t pid = 0;
    while ((entry = readdir(dir)) != nullptr) {
        if (entry->d_type != DT_DIR) continue;
        pid_t p = atoi(entry->d_name);
        if (p <= 0) continue;
        char cmdline[256]; sprintf(cmdline, "/proc/%d/cmdline", p);
        std::ifstream cmd(cmdline);
        std::string name;
        std::getline(cmd, name);
        if (name.find("com.george.lyra") != std::string::npos) {
            pid = p;
            break;
        }
    }
    closedir(dir);

    if (!pid) { std::cerr << "未找到 Lyra 进程\n"; return 1; }
    if (!AttachProcess(pid)) { std::cerr << "打开mem失败，需Root\n"; return 1; }

    std::cout << "已附加 PID: " << pid << "\n";
    std::cout << "GWorld 绝对地址: 0x" << std::hex << Offsets::GWorld << std::dec << "\n";

    while (true) {
        RunProjectileMagnet();
        std::this_thread::sleep_for(std::chrono::milliseconds(16));
    }

    close(mem_fd);
    return 0;
}