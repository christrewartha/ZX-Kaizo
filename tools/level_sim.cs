// A frame-exact port of the player physics in src/player.c, used by
// tools/check_levels.ps1 to prove every level can be finished. Keep it in
// step with player.c and player.h: same constants, same order of updates,
// same 8.8 fixed point wrapping. The enemies follow src/enemy.c and main.c's
// game_tick(): they move first, then the player, then they can hit.
//
// Written for the C# 5 compiler that Windows PowerShell 5.1's Add-Type uses.

using System;
using System.Collections.Generic;

public class LevelSim
{
    // Input bits (src/input.h)
    public const int IN_RIGHT = 0x01, IN_LEFT = 0x02, IN_DOWN = 0x04, IN_JUMP = 0x08;

    // Collision flags (src/room.h)
    public const byte TILE_SOLID = 0x01, TILE_PLATFORM = 0x02, TILE_HAZARD = 0x04, TILE_EXIT = 0x08,
                      TILE_TROPHY = 0x10;

    const int ROOM_ROWS = 22;
    const int ROOM_BOTTOM_Y = ROOM_ROWS * 8 - 16;
    const int SPR_MAX_X = 239;

    // src/player.h
    const int HB_X = 3, HB_W = 10, HEIGHT = 16;
    const int HURT_LEFT = 4, HURT_RIGHT = 11, HURT_BOTTOM = 15;
    // PLAYER_TOP and PLAYER_HURT_TOP: the Amiga's build sets them lower
    // (tools/build_amiga.ps1), and check_levels.ps1 -Amiga sets them here
    public static int TOP = 0, HURT_TOP = 3;
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
        public int trophy;          // carrying the trophy (main.c trophy_held)
    }

    // (ROOM_ROWS + 2) * 32 collision flags, as room_load() leaves room_coll
    readonly byte[] coll;

    public LevelSim(byte[] collision) { coll = collision; }

    // Enemies. Their patrols don't depend on the player, so each frame's
    // hit boxes are worked out in advance. Each enemy settles into a loop,
    // so where it is at frame t comes down to its phase in that loop.
    const int ENEMY_WALKER = 0, MAX_FRAMES = 20000;
    static readonly int[] HitLeft = { 2, 1 }, HitRight = { 13, 14 }, HitTop = { 4, 3 }, HitBottom = { 15, 10 };

    List<int[]> enemyDefs = new List<int[]>();
    int[] boxes;                // [frame][enemy][left, top, right, bottom]
    int[] loopStart, loopLength;

    public void AddEnemy(int type, int x, int y, int min, int max, int speed)
    {
        enemyDefs.Add(new int[] { type, x, y, min, max, speed });
        boxes = null;
    }

    void PlanEnemies()
    {
        int n = enemyDefs.Count;
        int[] pos = new int[n], vel = new int[n];
        List<Dictionary<int, int>> seenAt = new List<Dictionary<int, int>>();
        loopStart = new int[n];
        loopLength = new int[n];
        for (int i = 0; i < n; i++) {
            int[] d = enemyDefs[i];
            pos[i] = Fix(d[0] == ENEMY_WALKER ? d[1] : d[2]);
            vel[i] = d[5];
            seenAt.Add(new Dictionary<int, int>());
        }
        boxes = new int[(MAX_FRAMES + 1) * n * 4];

        for (int t = 1; t <= MAX_FRAMES; t++) {
            // enemies_update()
            for (int i = 0; i < n; i++) {
                int[] d = enemyDefs[i];
                pos[i] = (pos[i] + vel[i]) & 0xFFFF;
                int w = Whole(pos[i]);
                if (vel[i] > 0) {
                    if (w >= d[4]) { pos[i] = Fix(d[4]); vel[i] = -vel[i]; }
                } else if (w <= d[3]) {
                    pos[i] = Fix(d[3]); vel[i] = -vel[i];
                }
                int ex = d[0] == ENEMY_WALKER ? Whole(pos[i]) : d[1];
                int ey = d[0] == ENEMY_WALKER ? d[2] : Whole(pos[i]);
                int b = (t * n + i) * 4;
                boxes[b] = ex + HitLeft[d[0]];
                boxes[b + 1] = ey + HitTop[d[0]];
                boxes[b + 2] = ex + HitRight[d[0]];
                boxes[b + 3] = ey + HitBottom[d[0]];

                int first, key = (pos[i] << 1) | (vel[i] > 0 ? 1 : 0);
                if (loopLength[i] != 0) continue;
                if (seenAt[i].TryGetValue(key, out first)) {
                    loopStart[i] = first;
                    loopLength[i] = t - first;
                } else {
                    seenAt[i][key] = t;
                }
            }
        }
    }

    // Distance from the player within which an enemy's timing matters to
    // the search: about as far as the player travels in half a second
    const int NEAR = 48;

    // Which part of their loops the enemies now near the player are in.
    // States at the same place that differ only in this are the same to
    // the search. Far enemies are left out, which may hide a route that
    // needed a far enemy's timing but keeps the search small. Hashed to 64
    // bits; a clash could only hide a route too.
    ulong EnemyPhases(ref State p, int t, int bucket)
    {
        ulong h = 0;
        int px = Whole(p.x), py = Whole(p.y);
        int n = enemyDefs.Count;
        for (int i = 0; i < n; i++) {
            int b = (Math.Min(t, MAX_FRAMES) * n + i) * 4;
            if (px + 16 + NEAR < boxes[b] || px - NEAR > boxes[b + 2] ||
                py + 16 + NEAR < boxes[b + 1] || py - NEAR > boxes[b + 3]) continue;
            int phase = t;
            if (loopLength[i] != 0 && t >= loopStart[i])
                phase = loopStart[i] + (t - loopStart[i]) % loopLength[i];
            h = (h ^ (ulong)(phase / bucket + 1) ^ ((ulong)i << 40)) * 0x9E3779B97F4A7C15UL;
        }
        return h;
    }

    // enemies_hit() against the player's hurt box, on frame t
    bool HitByEnemy(ref State p, int t)
    {
        int n = enemyDefs.Count;
        if (n == 0) return false;
        if (t > MAX_FRAMES) return true;
        int left = (Whole(p.x) + HURT_LEFT) & 0xFF, right = (Whole(p.x) + HURT_RIGHT) & 0xFF;
        int top = (Whole(p.y) + HURT_TOP) & 0xFF, bottom = (Whole(p.y) + HURT_BOTTOM) & 0xFF;
        for (int i = 0; i < n; i++) {
            int b = (t * n + i) * 4;
            if (right >= boxes[b] && left <= boxes[b + 2] &&
                bottom >= boxes[b + 1] && top <= boxes[b + 3]) return true;
        }
        return false;
    }

    static int Whole(int f) { return (f >> 8) & 0xFF; }
    static int Fix(int n) { return (n << 8) & 0xFFFF; }

    byte At(int row, int col) { return coll[row * 32 + col]; }

    bool SolidColumn(int x, int top)
    {
        int col = (x & 0xFF) >> 3;
        int y = (top + TOP) & 0xFF;
        int last = ((top + HEIGHT - 1) & 0xFF) >> 3;
        for (int row = y >> 3; row <= last; row++)
            if ((At(row, col) & TILE_SOLID) != 0) return true;
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
            if ((RowFlags((py + TOP) & 0xFF, left) & TILE_SOLID) != 0) {
                ny = Fix((((py + TOP) & 0xFF) & 0xF8) + 8 - TOP);
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

    // One game_tick() on frame t, counting from 1 at the (re)start of the level
    public int Step(ref State p, int held, int t)
    {
        int ev = PlayerUpdate(ref p, held);
        if (ev == EV_NONE) {
            if (TouchingTrophy(ref p)) p.trophy = 1;
            if (HitByEnemy(ref p, t)) ev = EV_DIED;
        }
        return ev;
    }

    // touching_trophy() in main.c: any cell under the collision box, from
    // the top of the sprite (on the Amiga too, where the box is shorter)
    bool TouchingTrophy(ref State p)
    {
        int left = (Whole(p.x) + HB_X) & 0xFF;
        int c0 = left >> 3, c1 = ((left + HB_W - 1) & 0xFF) >> 3;
        int r0 = Whole(p.y) >> 3, r1 = ((Whole(p.y) + HEIGHT - 1) & 0xFF) >> 3;
        if (r1 >= ROOM_ROWS + 2) r1 = ROOM_ROWS + 1;
        for (int r = r0; r <= r1; r++)
            for (int c = c0; c <= c1; c++)
                if ((At(r, c) & TILE_TROPHY) != 0) return true;
        return false;
    }

    // player_update()
    int PlayerUpdate(ref State p, int held)
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

    // With enemies about, when matters too: 'phases' says where they are
    static ulong Key(ref State p, ulong phases)
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
        k = (k << 1) | U(p.trophy);
        return k ^ phases;
    }

    static readonly int[] Actions = {
        0, IN_RIGHT, IN_LEFT, IN_JUMP, IN_JUMP | IN_RIGHT, IN_JUMP | IN_LEFT, IN_DOWN
    };

    // Where the search is heading: the exit, or the trophy and then the exit
    class Target
    {
        public int left, right;     // its columns, in pixels
        public int[] cellDist;      // PlanCellDistances to it
    }
    Target exitTarget, trophyTarget;
    int trophyToExit;               // fewest frames from the trophy to the exit
    bool needTrophy;

    Target MakeTarget(byte flag, int up)
    {
        Target t = new Target();
        t.left = 255; t.right = 0;
        for (int i = 0; i < ROOM_ROWS * 32; i++) {
            if ((coll[i] & flag) == 0) continue;
            t.left = Math.Min(t.left, (i & 31) * 8);
            t.right = Math.Max(t.right, (i & 31) * 8 + 7);
        }
        if (t.left > t.right) return null;
        t.cellDist = PlanCellDistances(flag, up);
        return t;
    }

    int Estimate(ref State p)
    {
        if (needTrophy && p.trophy == 0) return EstimateTo(ref p, trophyTarget) + trophyToExit;
        return EstimateTo(ref p, exitTarget);
    }

    // Fewest frames the hurt box could take to reach the target's columns,
    // running flat out: never more than the real number, as A* needs
    int EstimateTo(ref State p, Target target)
    {
        int left = Whole(p.x) + HURT_LEFT, right = Whole(p.x) + HURT_RIGHT;
        int dx = 0;
        if (right < target.left) dx = target.left - right;
        else if (left > target.right) dx = left - target.right;
        int est = dx * 256 / RUN_MAX;

        // Routes that wind up and over walls: from the middle of the sprite
        // into a neighbouring cell, then the cell distance from there. It
        // must never drop faster than a frame per frame, or A* would keep
        // worse routes, so it measures to the edges of the cells.
        int cx = Whole(p.x) + 8, cy = Whole(p.y) + 8;
        int row = cy >> 3, col = cx >> 3, best = int.MaxValue;
        if (row >= ROOM_ROWS) return est;
        for (int r = row - 1; r <= row + 1; r++) {
            for (int c = col - 1; c <= col + 1; c++) {
                if (r < 0 || r >= ROOM_ROWS || c < 0 || c >= 32) continue;
                int d = target.cellDist[r * 32 + c];
                if (d == int.MaxValue) continue;
                int ox = Math.Max(0, Math.Max(c * 8 - cx, cx - (c * 8 + 7)));
                int oy = Math.Max(0, Math.Max(r * 8 - cy, cy - (r * 8 + 7)));
                d += Math.Max(ox * 16 * 256 / RUN_MAX, oy * 16 * 256 / MAX_FALL);
                if (d < best) best = d;
            }
        }
        // Less a step for being anywhere in that cell, and the reach of the
        // hurt box
        if (best != int.MaxValue) {
            best = (best - CELL_ACROSS - 48) / 16;
            if (best > est) est = best;
        }
        return est;
    }

    // Fewest frames (in 16ths) from each cell to a cell with 'flag' through
    // cells that aren't solid, at full run speed across and full fall or
    // jump speed up and down
    const int CELL_ACROSS = 8 * 256 * 16 / RUN_MAX, CELL_UPDOWN = 8 * 256 * 16 / MAX_FALL;

    int[] PlanCellDistances(byte flag, int up)
    {
        int n = ROOM_ROWS * 32;
        int[] cellDist = new int[n];
        List<int> open = new List<int>();
        for (int i = 0; i < n; i++) {
            cellDist[i] = int.MaxValue;
            if ((coll[i] & flag) != 0) { cellDist[i] = 0; open.Add(i); }
        }
        // Few enough cells to relax repeatedly instead of keeping a heap
        while (open.Count > 0) {
            List<int> next = new List<int>();
            foreach (int i in open) {
                int r = i >> 5, c = i & 31;
                for (int dr = -1; dr <= 1; dr++) {
                    for (int dc = -1; dc <= 1; dc++) {
                        int nr = r + dr, nc = c + dc;
                        if ((dr == 0 && dc == 0) || nr < 0 || nr >= ROOM_ROWS || nc < 0 || nc >= 32) continue;
                        int j = nr * 32 + nc;
                        if ((coll[j] & TILE_SOLID) != 0) continue;
                        int v = dr > 0 ? up : (dr < 0 ? CELL_UPDOWN : 0);
                        int d = cellDist[i] + Math.Max(dc != 0 ? CELL_ACROSS : 0, v);
                        if (d < cellDist[j]) { cellDist[j] = d; next.Add(j); }
                    }
                }
            }
            open = next;
        }
        return cellDist;
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
    // ask whether a less precise player could still finish. With 'trophy',
    // the route must pick up the trophy on the way. Returns the fewest frames
    // to the exit, or -1 if it can't be reached (or the search gave up after
    // maxStates). The inputs of the route found are left in LastPath.
    public int[] LastPath;

    public int Solve(int startX, int startY, int hold, int maxStates, out int explored)
    {
        return Solve(startX, startY, hold, maxStates, false, out explored);
    }

    public int Solve(int startX, int startY, int hold, int maxStates, bool trophy, out int explored)
    {
        Prepare(trophy);
        if (trophy && trophyTarget == null) { explored = 0; LastPath = null; return -1; }
        bool timed = enemyDefs.Count != 0;
        // Timing the enemies multiplies the states by the length of their
        // loops. To keep that tractable, count their phase in 8 frame steps
        // (so the search can only stand and wait in steps of 8 frames), and
        // push harder towards the exit: weighted A*, with climbing costed
        // at half the top speed. The route found is then real, but may not
        // be the fastest.
        int bucket = timed ? 8 : 1, weight = timed ? 2 : 1;
        return Search(Spawn(startX, startY), 0, hold, maxStates, MAX_FRAMES, null, bucket, weight, out explored);
    }

    bool prepared;

    void Prepare(bool trophy)
    {
        needTrophy = trophy;
        if (prepared) return;
        if (boxes == null) PlanEnemies();
        int up = enemyDefs.Count != 0 ? CELL_UPDOWN * 2 : CELL_UPDOWN;
        exitTarget = MakeTarget(TILE_EXIT, up);
        trophyTarget = MakeTarget(TILE_TROPHY, up);
        trophyToExit = 0;
        if (trophyTarget != null) {
            int best = int.MaxValue;
            for (int i = 0; i < ROOM_ROWS * 32; i++)
                if ((coll[i] & TILE_TROPHY) != 0) best = Math.Min(best, exitTarget.cellDist[i]);
            if (best != int.MaxValue) trophyToExit = Math.Max(0, (best - CELL_ACROSS - 48) / 16);
        }
        prepared = true;
    }

    // Where a local search is heading: back near where the route was at
    // some frame, holding the trophy if the route was by then
    class Checkpoint { public int x, y, trophy, frame; }

    static bool Reached(ref State p, Checkpoint cp)
    {
        return Math.Abs(Whole(p.y) - cp.y) <= 8 && Math.Abs(Whole(p.x) - cp.x) <= 8 && p.trophy >= cp.trophy;
    }

    // The search itself, from state s0 after frame t0. With a checkpoint it
    // also succeeds on reaching that, within maxFrames.
    int Search(State s0, int t0, int hold, int maxStates, int maxFrames, Checkpoint cp,
               int bucket, int weight, out int explored)
    {
        bool timed = enemyDefs.Count != 0;
        HashSet<ulong> seen = new HashSet<ulong>();
        List<State> states = new List<State>();
        List<int> frames = new List<int>();
        List<int> parents = new List<int>();
        List<int> acts = new List<int>();
        heap.Clear();
        LastPath = null;
        explored = 0;

        if (cp != null && Reached(ref s0, cp)) { LastPath = new int[0]; return 0; }
        seen.Add(Key(ref s0, 0UL));
        states.Add(s0);
        frames.Add(0);
        parents.Add(-1);
        acts.Add(0);
        Push(Heuristic(ref s0, cp), 0);

        while (heap.Count > 0) {
            int index = Pop();
            State cur = states[index];
            int g = frames[index];
            if (++explored > maxStates) return -1;
            if (g + hold > maxFrames) continue;

            foreach (int a in Actions) {
                State n = cur;
                int ev = EV_NONE, f;
                bool done = false;
                for (f = 0; f < hold && ev == EV_NONE; f++) {
                    ev = Step(ref n, a, t0 + g + f + 1);
                    if (ev == EV_NONE && cp != null && Reached(ref n, cp)) { f++; done = true; break; }
                }
                if (ev == EV_DONE && (!needTrophy || n.trophy != 0)) done = true;
                if (done) {
                    List<int> path = new List<int>();
                    for (int k = 0; k < f; k++) path.Add(a);
                    for (int q = index; parents[q] >= 0; q = parents[q])
                        for (int k = 0; k < hold; k++) path.Add(acts[q]);
                    path.Reverse();
                    LastPath = path.ToArray();
                    return g + f;
                }
                if (ev != EV_NONE) continue;     // died, or left without the trophy
                if (seen.Add(Key(ref n, timed ? EnemyPhases(ref n, t0 + g + hold, bucket) : 0UL))) {
                    states.Add(n);
                    frames.Add(g + hold);
                    parents.Add(index);
                    acts.Add(a);
                    Push(g + hold + weight * Heuristic(ref n, cp), states.Count - 1);
                }
            }
        }
        return -1;
    }

    int Heuristic(ref State p, Checkpoint cp)
    {
        if (cp == null) return Estimate(ref p);
        return Math.Max(Math.Abs(Whole(p.x) - cp.x) * 256 / RUN_MAX, Math.Abs(Whole(p.y) - cp.y) * 256 / MAX_FALL);
    }

    // How forgiving each jump of a route is. Each jump is pressed a frame
    // early and late, and let go of a frame early and late: 9 ways, counting
    // the route's own. A way counts if the player lives through it and can
    // get back to where the route is RECOVER_FRAMES after letting go (or to the exit), changing
    // the inputs after it as a person would. Fills in JumpFrames (the frame
    // each jump is pressed), JumpX/JumpY (where) and JumpScores (how many of
    // the 9 ways work), and returns the lowest score: 9 if there are none.
    public int[] JumpFrames, JumpScores, JumpX, JumpY;
    const int RECOVER_FRAMES = 30;

    public int Tolerance(int startX, int startY, int[] path, bool trophy)
    {
        Prepare(trophy);
        int n = path.Length;
        State[] at = new State[n + 1];
        at[0] = Spawn(startX, startY);
        for (int f = 1; f <= n; f++) {
            at[f] = at[f - 1];
            Step(ref at[f], path[f - 1], f);
        }
        List<int> jf = new List<int>(), js = new List<int>(), jx = new List<int>(), jy = new List<int>();
        int worst = 9;
        int[] saved = LastPath;

        for (int p = 1; p <= n; p++) {
            bool down = (path[p - 1] & IN_JUMP) != 0;
            bool before = p > 1 && (path[p - 2] & IN_JUMP) != 0;
            if (!down || before) continue;
            int r = p;
            while (r <= n && (path[r - 1] & IN_JUMP) != 0) r++;

            // Back on the route's track a little after the jump
            Checkpoint cp = null;
            if (r + RECOVER_FRAMES <= n) {
                State c = at[r + RECOVER_FRAMES];
                cp = new Checkpoint();
                cp.x = Whole(c.x); cp.y = Whole(c.y); cp.trophy = c.trophy; cp.frame = r + RECOVER_FRAMES;
            }

            int score = 0;
            for (int d0 = -1; d0 <= 1; d0++) {
                for (int d1 = -1; d1 <= 1; d1++) {
                    int ps = p + d0, rs = (r > n) ? r : r + d1;
                    if (ps < 1 || rs <= ps) continue;
                    if (Recovers(at, path, p, r, ps, rs, cp, trophy)) score++;
                }
            }
            jf.Add(p); js.Add(score); jx.Add(Whole(at[p - 1].x)); jy.Add(Whole(at[p - 1].y));
            if (score < worst) worst = score;
        }
        JumpFrames = jf.ToArray(); JumpScores = js.ToArray(); JumpX = jx.ToArray(); JumpY = jy.ToArray();
        LastPath = saved;
        return worst;
    }

    bool Recovers(State[] at, int[] path, int p, int r, int ps, int rs, Checkpoint cp, bool trophy)
    {
        int n = path.Length;
        int w0 = Math.Min(p, ps), w1 = Math.Min(Math.Max(r, rs), n + 1);
        State s = at[w0 - 1];
        for (int f = w0; f < w1; f++) {
            int a = (f <= n ? path[f - 1] : 0) & ~IN_JUMP;
            if (f >= ps && f < rs) a |= IN_JUMP;
            int ev = Step(ref s, a, f);
            if (ev == EV_DONE) return !trophy || s.trophy != 0;
            if (ev != EV_NONE) return false;
        }
        int until = cp == null ? n : cp.frame;
        int explored;
        return Search(s, w1 - 1, 1, 150000, Math.Max(until - (w1 - 1), 0) + 40, cp, 4, 2, out explored) >= 0;
    }
}
