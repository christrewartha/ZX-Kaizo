// A frame-exact port of the player physics in src/player.c, used by
// tools/check_levels.ps1 to prove every level can be finished. Keep it in
// step with player.c and player.h: same constants, same order of updates,
// same 8.8 fixed point wrapping. Enemies are not simulated.
//
// Written for the C# 5 compiler that Windows PowerShell 5.1's Add-Type uses.

using System;
using System.Collections.Generic;

public class LevelSim
{
    // Input bits (src/input.h)
    public const int IN_RIGHT = 0x01, IN_LEFT = 0x02, IN_DOWN = 0x04, IN_JUMP = 0x08;

    // Collision flags (src/room.h)
    public const byte TILE_SOLID = 0x01, TILE_PLATFORM = 0x02, TILE_HAZARD = 0x04, TILE_EXIT = 0x08;

    const int ROOM_ROWS = 22;
    const int ROOM_BOTTOM_Y = ROOM_ROWS * 8 - 16;
    const int SPR_MAX_X = 239;

    // src/player.h
    const int HB_X = 3, HB_W = 10, HEIGHT = 16;
    const int HURT_LEFT = 4, HURT_RIGHT = 11, HURT_TOP = 3, HURT_BOTTOM = 15;
    const int WALL_LEFT = 1, WALL_RIGHT = 2;

    // src/player.c
    const int RUN_MAX = 0x0180, GROUND_ACCEL = 0x0028, GROUND_DECEL = 0x0030, TURN_ACCEL = 0x0060;
    const int AIR_ACCEL = 0x0020, AIR_DECEL = 0x0010;
    const int JUMP_VEL = -0x0400, GRAV_RISE = 0x0038, GRAV_CUT = 0x00A0, GRAV_FALL = 0x0050;
    const int APEX_BAND = 0x0080, MAX_FALL = 0x0400;
    const int COYOTE_FRAMES = 4, BUFFER_FRAMES = 6;
    const int WALL_JUMP_VX = RUN_MAX, WALL_JUMP_VY = -0x0380, WALL_SLIDE_MAX = 0x00C0;
    const int WALL_COYOTE_FRAMES = 4, WALL_LOCK_FRAMES = 10;

    public const int EV_NONE = 0, EV_DIED = 1, EV_DONE = 2;

    public struct State
    {
        public int x, y;            // ufix8, 0..0xFFFF
        public int vx, vy;          // sfix8
        public int onGround, jumping, coyote, jumpBuffer;
        public int wall, wallSide, wallCoyote, wallLock;
        public int prevHeld;
    }

    // (ROOM_ROWS + 2) * 32 collision flags, as room_load() leaves room_coll
    readonly byte[] coll;

    public LevelSim(byte[] collision) { coll = collision; }

    static int Whole(int f) { return (f >> 8) & 0xFF; }
    static int Fix(int n) { return (n << 8) & 0xFFFF; }

    byte At(int row, int col) { return coll[row * 32 + col]; }

    bool SolidColumn(int x, int top)
    {
        int row = (top & 0xF8) >> 3, col = (x & 0xFF) >> 3;
        if (((At(row, col) | At(row + 1, col)) & TILE_SOLID) != 0) return true;
        if ((top & 7) != 0 && (At(row + 2, col) & TILE_SOLID) != 0) return true;
        return false;
    }

    int RowFlags(int y, int left)
    {
        int row = (y & 0xF8) >> 3;
        int c = left >> 3, last = ((left + HB_W - 1) & 0xFF) >> 3;
        int f = 0;
        do { f |= At(row, c); } while (++c <= last);
        return f;
    }

    // touching_hazard(): the flags under the shrunken hurt box
    int TouchFlags(ref State p)
    {
        int left = ((Whole(p.x) + HURT_LEFT) & 0xFF) >> 3;
        int right = ((Whole(p.x) + HURT_RIGHT) & 0xFF) >> 3;
        int top = (Whole(p.y) + HURT_TOP) & 0xFF;
        int bottom = (Whole(p.y) + HURT_BOTTOM - 3) & 0xFF;
        int tr = (top & 0xF8) >> 3, br = (bottom & 0xF8) >> 3;
        return At(tr, left) | At(tr, right) | At(br, left) | At(br, right);
    }

    public static State Spawn(int x, int y)
    {
        State p = new State();
        p.x = Fix(x);
        p.y = Fix(y);
        return p;
    }

    static void UpdateHorizontal(ref State p, int held)
    {
        int vx = p.vx, accel, decel, turn;

        if (p.wallLock != 0) { p.wallLock--; return; }

        if (p.onGround != 0) { accel = GROUND_ACCEL; decel = GROUND_DECEL; turn = TURN_ACCEL; }
        else { accel = AIR_ACCEL; decel = AIR_DECEL; turn = AIR_ACCEL; }

        if ((held & IN_RIGHT) != 0) {
            vx += (vx < 0) ? turn : accel;
            if (vx > RUN_MAX) vx = RUN_MAX;
        } else if ((held & IN_LEFT) != 0) {
            vx -= (vx > 0) ? turn : accel;
            if (vx < -RUN_MAX) vx = -RUN_MAX;
        } else if (vx > 0) {
            vx -= decel;
            if (vx < 0) vx = 0;
        } else if (vx < 0) {
            vx += decel;
            if (vx > 0) vx = 0;
        }
        p.vx = vx;
    }

    static void UpdateVertical(ref State p, int held, int pressed)
    {
        bool holding = p.jumping != 0 && (held & IN_JUMP) != 0;

        if ((held & IN_JUMP) == 0) p.jumping = 0;

        if ((pressed & IN_JUMP) != 0) p.jumpBuffer = BUFFER_FRAMES;
        else if (p.jumpBuffer != 0) p.jumpBuffer--;

        if (p.jumpBuffer != 0) {
            if (p.coyote != 0) {
                p.vy = JUMP_VEL;
            } else if (p.wallCoyote != 0) {
                p.vy = WALL_JUMP_VY;
                p.vx = (p.wallSide == WALL_LEFT) ? WALL_JUMP_VX : -WALL_JUMP_VX;
                p.wallLock = WALL_LOCK_FRAMES;
            }
            if (p.coyote != 0 || p.wallCoyote != 0) {
                p.jumping = 1;
                p.jumpBuffer = 0;
                p.coyote = 0;
                p.wallCoyote = 0;
                p.onGround = 0;
                holding = (held & IN_JUMP) != 0;
            }
        }

        bool sliding = p.wall != 0 &&
                       (held & ((p.wall == WALL_LEFT) ? IN_LEFT : IN_RIGHT)) != 0;

        int vy = p.vy, g;
        if (vy < 0) g = holding ? GRAV_RISE : GRAV_CUT;
        else g = GRAV_FALL;
        if (holding && vy > -APEX_BAND && vy < APEX_BAND) g >>= 1;

        vy += g;
        if (vy > MAX_FALL) vy = MAX_FALL;
        if (sliding && vy > WALL_SLIDE_MAX) vy = WALL_SLIDE_MAX;
        if (vy >= APEX_BAND) p.jumping = 0;
        p.vy = vy;
    }

    void MoveHorizontal(ref State p)
    {
        int nx = (p.x + p.vx) & 0xFFFF;

        // The screen edges are walls
        if (p.vx < 0 && nx > p.x) { p.x = 0; p.vx = 0; return; }
        if (Whole(nx) > SPR_MAX_X) { p.x = Fix(SPR_MAX_X); p.vx = 0; return; }

        int px = Whole(nx), py = Whole(p.y), edge;
        if (p.vx > 0) {
            edge = (px + HB_X + HB_W - 1) & 0xFF;
            if (SolidColumn(edge, py)) {
                nx = Fix((edge & 0xF8) - (HB_X + HB_W));
                p.vx = 0;
            }
        } else if (p.vx < 0) {
            edge = (px + HB_X) & 0xFF;
            if (SolidColumn(edge, py)) {
                nx = Fix((edge & 0xF8) + 8 - HB_X);
                p.vx = 0;
            }
        }
        p.x = nx;
    }

    // Returns false when the player falls out of the bottom
    bool MoveVertical(ref State p, int held)
    {
        int ny = (p.y + p.vy) & 0xFFFF;
        int oldPy = Whole(p.y);
        int left = (Whole(p.x) + HB_X) & 0xFF;

        // The top of the screen is an invisible ceiling
        if (p.vy < 0 && ny > p.y) {
            p.y = 0;
            p.vy = 0;
            p.jumping = 0;
            p.onGround = 0;
            return true;
        }
        int py = Whole(ny);
        if (py > ROOM_BOTTOM_Y) { p.y = ny; return false; }

        p.onGround = 0;
        if (p.vy >= 0) {
            int foot = (py + HEIGHT) & 0xFF;
            int flags = RowFlags(foot, left);
            if ((flags & TILE_SOLID) != 0 ||
                ((flags & TILE_PLATFORM) != 0 && (held & IN_DOWN) == 0 &&
                 oldPy + HEIGHT <= (foot & 0xF8))) {
                ny = Fix((foot & 0xF8) - HEIGHT);
                p.vy = 0;
                p.onGround = 1;
                p.jumping = 0;
            }
        } else {
            if ((RowFlags(py, left) & TILE_SOLID) != 0) {
                ny = Fix((py & 0xF8) + 8);
                p.vy = 0;
                p.jumping = 0;
            }
        }
        p.y = ny;
        return true;
    }

    void DetectWall(ref State p)
    {
        int left = (Whole(p.x) + HB_X) & 0xFF;
        int top = Whole(p.y);

        p.wall = 0;
        if (p.onGround == 0) {
            if (SolidColumn(left - 1, top)) p.wall = WALL_LEFT;
            else if (SolidColumn(left + HB_W, top)) p.wall = WALL_RIGHT;
        }
        if (p.wall != 0) {
            p.wallSide = p.wall;
            p.wallCoyote = WALL_COYOTE_FRAMES;
        } else if (p.wallCoyote != 0) {
            p.wallCoyote--;
        }
    }

    // player_update()
    public int Step(ref State p, int held)
    {
        int pressed = held & ~p.prevHeld;
        p.prevHeld = held;

        UpdateHorizontal(ref p, held);
        UpdateVertical(ref p, held, pressed);
        MoveHorizontal(ref p);
        if (!MoveVertical(ref p, held)) return EV_DIED;

        if (p.onGround != 0) p.coyote = COYOTE_FRAMES;
        else if (p.coyote != 0) p.coyote--;
        DetectWall(ref p);

        int t = TouchFlags(ref p);
        if ((t & TILE_HAZARD) != 0) return EV_DIED;
        if ((t & TILE_EXIT) != 0) return EV_DONE;
        return EV_NONE;
    }

    // What the search treats as the same state. Velocities are bucketed
    // and the countdowns reduced to "running or not", so the search keeps
    // the first state it meets in each bucket and drops near-duplicates.
    // Every state it keeps was really reached, so a path it finds is a real
    // path; it can only miss the very tightest ones.
    static ulong U(int v) { return (ulong)(uint)v; }

    static ulong Key(ref State p)
    {
        ulong k = U(Whole(p.x));
        k = (k << 8) | U(Whole(p.y));
        k = (k << 6) | U((p.vx >> 5) & 0x3F);
        k = (k << 6) | U((p.vy >> 6) & 0x3F);
        k = (k << 1) | U(p.onGround);
        k = (k << 1) | U(p.jumping);
        k = (k << 1) | U(p.coyote != 0 ? 1 : 0);
        k = (k << 2) | U(p.wall);
        k = (k << 2) | U(p.wallSide);
        k = (k << 1) | U(p.wallCoyote != 0 ? 1 : 0);
        k = (k << 2) | U(p.wallLock >> 2);
        k = (k << 1) | U((p.prevHeld & IN_JUMP) != 0 ? 1 : 0);
        return k;
    }

    static readonly int[] Actions = {
        0, IN_RIGHT, IN_LEFT, IN_JUMP, IN_JUMP | IN_RIGHT, IN_JUMP | IN_LEFT, IN_DOWN
    };

    // Fewest frames the hurt box could take to reach the exit's columns,
    // running flat out: never more than the real number, as A* needs
    int exitLeft, exitRight;

    int Estimate(ref State p)
    {
        int left = Whole(p.x) + HURT_LEFT, right = Whole(p.x) + HURT_RIGHT;
        int dx = 0;
        if (right < exitLeft) dx = exitLeft - right;
        else if (left > exitRight) dx = left - exitRight;
        return dx * 256 / RUN_MAX;
    }

    // Binary min-heap of state indices, ordered by frames so far + estimate
    List<long> heap = new List<long>();

    void Push(int f, int index)
    {
        heap.Add(((long)f << 32) | (uint)index);
        int i = heap.Count - 1;
        while (i > 0) {
            int parent = (i - 1) / 2;
            if (heap[parent] <= heap[i]) break;
            long t = heap[parent]; heap[parent] = heap[i]; heap[i] = t;
            i = parent;
        }
    }

    int Pop()
    {
        long top = heap[0];
        heap[0] = heap[heap.Count - 1];
        heap.RemoveAt(heap.Count - 1);
        int i = 0;
        while (true) {
            int l = i * 2 + 1, r = l + 1, m = i;
            if (l < heap.Count && heap[l] < heap[m]) m = l;
            if (r < heap.Count && heap[r] < heap[m]) m = r;
            if (m == i) break;
            long t = heap[m]; heap[m] = heap[i]; heap[i] = t;
            i = m;
        }
        return (int)(top & 0xFFFFFFFF);
    }

    // A* search over inputs. 'hold' is how many frames each input must be
    // held before it can change: 1 allows frame-perfect play, higher values
    // ask whether a less precise player could still finish. Returns the
    // fewest frames to the exit, or -1 if it can't be reached (or the search
    // gave up after maxStates).
    public int Solve(int startX, int startY, int hold, int maxStates, out int explored)
    {
        exitLeft = 255; exitRight = 0;
        for (int i = 0; i < ROOM_ROWS * 32; i++) {
            if ((coll[i] & TILE_EXIT) == 0) continue;
            exitLeft = Math.Min(exitLeft, (i & 31) * 8);
            exitRight = Math.Max(exitRight, (i & 31) * 8 + 7);
        }

        HashSet<ulong> seen = new HashSet<ulong>();
        List<State> states = new List<State>();
        List<int> frames = new List<int>();
        heap.Clear();

        State s = Spawn(startX, startY);
        seen.Add(Key(ref s));
        states.Add(s);
        frames.Add(0);
        Push(Estimate(ref s), 0);
        explored = 0;

        while (heap.Count > 0) {
            int index = Pop();
            State cur = states[index];
            int g = frames[index];
            if (++explored > maxStates) return -1;

            foreach (int a in Actions) {
                State n = cur;
                int ev = EV_NONE, f;
                for (f = 0; f < hold && ev == EV_NONE; f++) ev = Step(ref n, a);
                if (ev == EV_DONE) return g + f;
                if (ev == EV_DIED) continue;
                if (seen.Add(Key(ref n))) {
                    states.Add(n);
                    frames.Add(g + hold);
                    Push(g + hold + Estimate(ref n), states.Count - 1);
                }
            }
        }
        return -1;
    }
}
