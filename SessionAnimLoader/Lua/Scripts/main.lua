-- Session Anim Loader: Lua half of the clip player (UE4SS 3.x, Session: Skate Sim). Every flip trick per stance, base poses.
-- 2026-11-26 v0.19: multiplayer through the SessionOpenMP mod channel (section "multiplayer" below): our clips are played on our skater on the other players' screens.
-- 2026-11-26 v0.21: clips are found by CONTENT on the other PCs (any file name); clips of other players go in clips\others\.
-- It finds the player's skater, gives its mesh and bone table (parents + names) to SessionAnimLoaderNative (ASN_*),
-- sends it the game state every frame (manual, nose manual, goofy, switch, ManualRatio, trick + pop + phase), rescans
-- the Mods\SessionAnimLoader\clips folder every 1 s, and handles the keys: Ctrl+F3 = mod on/off, Ctrl+F4 = pose capture.
-- Results: [SessionAnimLoader] lines in UE4SS.log + SessionAnimLoader.native.log next to the exe.
-- Rules: everything runs in the per-frame hook (game thread), the keys only raise a flag.

local VERSION = "v0.30"
local TAG = "[SessionAnimLoader] "
local HOOK_FN = "/Script/SessionGame.SkaterCharacterBase:GetSkateboardingAnimParams"

local cfg = {
    ScanEvery = 1.0,     -- s between two rescans of the clips folder
    StatsEvery = 20,     -- s between two counter lines
}

local function log(m) print(TAG .. m .. "\n") end
local function q(fn, d)
    local ok, v = pcall(fn)
    if ok and v ~= nil then return v end
    return d
end
local function valid(o)
    if not o then return false end
    local ok, r = pcall(function() return o:IsValid() end)
    return ok and r == true
end
local function addr(o) return q(function() return o:GetAddress() end, nil) end
local function native() return type(ASN_SetMesh) == "function" and type(ASN_SetState) == "function" end
local function b01(v) return (v == true) and 1 or 0 end

local P = {
    sk = nil, skAddr = nil, mesh = nil, meshAddr = nil, adoptT = 0, rigFor = nil, inst = nil,
    state = nil, pollT = 0, statT = 0, scanT = 0, on = true,
    wantToggle = false, wantCapture = false, capturePending = false, captureT = 0,
    told = false, errTold = false, configured = false,
    manual = false, nose = false, goofy = nil, sw = nil, clip = "",
    rMin = nil, rMax = nil,
    trickPending = false, kickflip = false, popSerial = 0, phase = 0, popT = 0, groundT = nil,
    grind = false, powerslide = false, grab = false,
    replay = false,
    popZ = nil,   -- 2026-11-25 landing sync: actor Z at the pop (height above the takeoff during the trick)
    deck = nil, boardTries = 0, boardT = 0,   -- 2026-11-27: the visible board's deck component (searched at adoption)
}

local function stats(why)
    log((why and (why .. " | ") or "") .. tostring(q(function() return ASN_Stats() end, "?")))
end

local function sendRig()
    local mesh = P.mesh
    local n = tonumber(q(function() return mesh:GetNumBones() end, 0)) or 0
    local par, names, pos = {}, {}, {}
    local posOk = 0
    for i = 0, n - 1 do
        local nm = q(function() return mesh:GetBoneName(i) end, nil)
        local s = nm and q(function() return nm:ToString() end, nil) or ("os_" .. i)
        local pn = nm and q(function() return mesh:GetParentBone(nm) end, nil)
        local pi = pn and tonumber(q(function() return mesh:GetBoneIndex(pn) end, -1)) or -1
        par[#par + 1] = tostring(pi)
        names[#names + 1] = s
        -- reference local position (as in the P0 probe), in integer hundredths of a cm: no decimal separator
        local rp = q(function() local v = mesh:GetRefPosePosition(i) return { v.X, v.Y, v.Z } end, nil)
        if rp then posOk = posOk + 1 else rp = { 0, 0, 0 } end
        for k = 1, 3 do pos[#pos + 1] = string.format("%d", math.floor((tonumber(rp[k]) or 0) * 100 + 0.5)) end
    end
    local cnt = tonumber(q(function() return ASN_SetRig(table.concat(par, " "), table.concat(names, " "), table.concat(pos, " ")) end, 0)) or 0
    log(string.format("bone table sent: %d bones (reference positions read: %d), accepted: %s", n, posOk, cnt == n and "yes" or "NO"))
end

local function scanClips()
    local out = tostring(q(function() return ASN_ScanClips() end, ""))
    if out ~= "" then
        for line in string.gmatch(out, "[^\n]+") do log("clips: " .. line) end
    end
end

-- ------------------------------------------------------------------ 2026-11-27 the visible board
-- The board seen in game is its own actor; its "Flipper" component is the deck (the skeleton's board bones do not
-- flip). No property of the skater leads to it (probe of 2026-11-27), so it is searched with FindAllOf ONLY after the
-- bone table is sent (map load, new skater, new mesh): a few tries at most, then the result is kept. Never during
-- play: if the board is lost, there is no board in the recordings until the next time.
local BOARD = { CLASS = "PBP_SkateboardEx_C", NAME = "Skateboard", NEAR = 200, TRIES = 5, EVERY = 2.0 }

local function componentNamed(actor, want)
    local cls = q(function() return StaticFindObject("/Script/Engine.SceneComponent") end, nil)
    local list = cls and q(function() return actor:K2_GetComponentsByClass(cls) end, nil)
    if not list then return nil end
    local n = q(function() return #list end, nil) or q(function() return list:GetArrayNum() end, 0)
    for i = 1, tonumber(n) or 0 do
        local c = q(function() return list[i]:get() end, nil) or q(function() return list[i] end, nil)
        if valid(c) and q(function() return c:GetFName():ToString() end, "") == want then return c end
    end
    return nil
end

-- the deck of OUR board: an actor named "Skateboard" (another player's board may carry the name too: the nearest one)
local function findDeck(sk)
    local all = q(function() return FindAllOf(BOARD.CLASS) end, nil)
    if type(all) ~= "table" then return nil end
    local here = q(function() local v = sk:K2_GetActorLocation() return { v.X, v.Y, v.Z } end, nil)
    if not here then return nil end
    local best, bestD = nil, BOARD.NEAR * BOARD.NEAR
    for _, b in ipairs(all) do
        if valid(b) and q(function() return b:GetFName():ToString() end, "") == BOARD.NAME then
            local c = componentNamed(b, "Flipper")
            local d = c and q(function()
                local v = c:K2_GetComponentLocation()
                return (v.X - here[1]) ^ 2 + (v.Y - here[2]) ^ 2 + (v.Z - here[3]) ^ 2
            end, nil)
            if d and d <= bestD then best, bestD = c, d end
        end
    end
    return best
end

local function boardSearch(sk, now)
    if P.deck or P.boardTries <= 0 or now - P.boardT < BOARD.EVERY then return end
    P.boardT, P.boardTries = now, P.boardTries - 1
    P.deck = findDeck(sk)
    if P.deck then
        P.boardTries = 0
        log("board: found, it will be recorded with the moves")
    elseif P.boardTries == 0 then
        log("board: not found near the skater: the moves are recorded without it (until the next map or skater)")
    end
end

-- mesh and deck in world space -> C++ (every frame once the board is known: the frame a recording starts needs it)
local function sendBoard()
    if not P.deck or type(ASN_SetBoard) ~= "function" then return end
    if not valid(P.deck) then
        P.deck = nil
        log("board: lost: the moves are recorded without it (until the next map or skater)")
        return
    end
    local read, why = pcall(function()
        local m = P.mesh
        local ml = m:K2_GetComponentLocation()
        local mx, my, mz = ml.X, ml.Y, ml.Z
        local mq = m:K2_GetComponentToWorld().Rotation
        local qx, qy, qz, qw = mq.X, mq.Y, mq.Z, mq.W
        local ms = m:K2_GetComponentScale()
        local sx, sy, sz = ms.X, ms.Y, ms.Z
        local dl = P.deck:K2_GetComponentLocation()
        local dx, dy, dz = dl.X, dl.Y, dl.Z
        local dq = P.deck:K2_GetComponentToWorld().Rotation
        ASN_SetBoard(mx, my, mz, qx, qy, qz, qw, sx, sy, sz, dx, dy, dz, dq.X, dq.Y, dq.Z, dq.W)
    end)
    if not read and not P.boardErrTold then
        P.boardErrTold = true
        log("board: cannot be read: " .. tostring(why))
    end
end

-- name of the current FlipTrick (Name property, else the end of the object's full name); nil if unreadable
local function trickName(ai)
    local ft = q(function() return ai.FlipTrick end, nil)
    if not valid(ft) then return nil end
    local n = q(function() return ft.Name:ToString() end, nil)
    if n == nil then
        local full = q(function() return ft:GetFullName() end, nil)
        n = full and string.match(full, "TRICK_([%w_]+)$") or nil
    end
    return n
end

-- v0.27: the name the game shows for the current FlipTrick (DisplayName, per stance); "" if unreadable
local function trickShownName(ai)
    return tostring(q(function() return ai.FlipTrick.DisplayName:ToString() end, "") or "")
end

-- trick: pop on the rising edge of IsTrickPending, phase = max(phase, observed step), ends on the ground or after 2 s
-- Clock: os.clock() (same as the rest of the script). Returns trick (bool), popSerial, phase; any read failure = no trick.
local function trickStep(ai, now)
    local pending = q(function() return ai.IsTrickPending end, false) == true
    if pending and not P.trickPending then
        local name = trickName(ai)
        -- P6 : every flip trick (RGS_/SWS_/FKS_/NLS_ + name); the DLL matches clips and figures by name and stance
        if name then
            P.popSerial = P.popSerial + 1
            P.kickflip, P.phase, P.popT = true, 0, now
            P.groundT = nil
            P.popZ = nil   -- read by jumpState on this frame
            local shown = trickShownName(ai)
            pcall(function() ASN_SetTrick(name, shown) end)
            log("trick recognised: " .. name)
        else
            if P.kickflip then P.kickflip, P.phase = false, 0 end
        end
    end
    P.trickPending = pending
    if P.kickflip then
        local grounded = q(function() return ai.IsGrounded end, true) == true
        local obs = 0
        if not grounded then obs = 1 end
        -- steps 2/3/4 only once airborne (avoids a CatchOrientState left over from the previous trick at the pop)
        if P.phase >= 1 or obs >= 1 then
            if q(function() return ai.IsLanding end, false) == true then obs = 4
            elseif (tonumber(q(function() return ai.CatchOrientState end, 0)) or 0) ~= 0 then obs = 3
            elseif q(function() return ai.IsBoardFlipping end, false) == true then obs = 2 end
        end
        if obs > P.phase then P.phase = obs end
        local age = now - P.popT
        -- since when the ground has been held (a single ground frame right after takeoff does not count)
        if grounded then
            if P.groundT == nil then P.groundT = now end
        else
            P.groundT = nil
        end
        local groundFor = 0
        if P.groundT ~= nil then groundFor = now - P.groundT end
        -- return to the ground only counts after being airborne (ground stays true ~0.19 s after the pop), held 0.03 s
        if age > 2.0 or (age > 0.2 and P.phase >= 1 and grounded and groundFor >= 0.03) then
            P.kickflip, P.phase = false, 0
            P.groundT = nil
        end
    end
    return P.kickflip, P.popSerial, P.phase
end

-- 2026-11-25 landing sync: vertical speed (cm/s, up +), height above the takeoff (cm), pop height (_trickPopHeight)
-- while a trick is on; ok = 0 when unreadable (the DLL then plays the hang time in real time)
local function jumpState(sk)
    if not P.kickflip then P.popZ = nil return 0, 0, 0, 0 end
    local z = q(function() return sk:K2_GetActorLocation().Z end, nil)
    local vz = q(function() return sk:GetVelocity().Z end, nil)
    if vz == nil then vz = q(function() return sk.CharacterMovement.Velocity.Z end, nil) end
    local h = tonumber(q(function() return sk._trickPopHeight end, 0)) or 0
    z, vz = tonumber(z), tonumber(vz)
    if z == nil or vz == nil then return 0, 0, 0, h end
    if P.popZ == nil then P.popZ = z end
    return vz, z - P.popZ, 1, h
end

-- game state, read on the AnimInstance (cached, read again when no longer valid)
local function readState(now)
    if not valid(P.inst) then
        P.inst = q(function() return P.mesh:GetAnimInstance() end, nil)
        if not valid(P.inst) then return nil end
    end
    local ai = P.inst
    local okt, kf, serial, phase = pcall(trickStep, ai, now)
    if not okt then kf, serial, phase = false, P.popSerial, 0 end   -- the manual path does not depend on tricks
    return q(function() return ai.IsInManual end, false) == true,
           q(function() return ai.IsInNoseManual end, false) == true,
           q(function() return ai.IsSkatingGoofy end, false) == true,
           q(function() return ai.IsSkatingSwitch end, false) == true,
           tonumber(q(function() return ai.ManualRatio end, 0)) or 0,
           kf, serial, phase,
           q(function() return ai.IsGrinding end, false) == true,
           q(function() return ai.IsInPowerslideStance end, false) == true,
           (tonumber(q(function() return ai.GrabState end, 0)) or 0) ~= 0,
           -- P6 lot 4: base poses (on foot, riding, crouch, landing)
           q(function() return ai.IsOnBoard end, true) ~= false,
           q(function() return ai.IsGrounded end, false) == true,
           q(function() return ai.IsCranking end, false) == true,
           (q(function() return ai.IsLanding end, false) == true) or (q(function() return ai.IsJustLanded end, false) == true),
           tonumber(q(function() return ai.FootPositionType end, -1)) or -1,
           tonumber(q(function() return ai.Speed end, 0)) or 0,
           q(function() return ai.IsPushing end, false) == true
end

local function noteState(manual, nose, goofy, sw, ratio)
    local inAny = manual or nose
    if inAny then
        if P.rMin == nil or ratio < P.rMin then P.rMin = ratio end
        if P.rMax == nil or ratio > P.rMax then P.rMax = ratio end
    end
    if manual ~= P.manual or nose ~= P.nose then
        local was = P.manual and "manual" or (P.nose and "nose manual" or nil)
        local now = manual and "manual" or (nose and "nose manual" or nil)
        if was and P.rMin then log(string.format("left %s (ManualRatio seen: %.3f to %.3f)", was, P.rMin, P.rMax)) end
        if now then log("entered " .. now) end
        if not inAny then P.rMin, P.rMax = nil, nil end
        P.manual, P.nose = manual, nose
    end
    if goofy ~= P.goofy or sw ~= P.sw then
        P.goofy, P.sw = goofy, sw
        log(string.format("stance: %s%s -> mirror %s", goofy and "goofy" or "regular", sw and " + switch" or "", (goofy ~= sw) and "YES" or "no"))
    end
    local clip = tostring(q(function() return ASN_Clip() end, ""))
    if clip ~= P.clip then
        P.clip = clip
        log(clip ~= "" and ("current clip: " .. clip) or "no current clip any more")
    end
end

-- ------------------------------------------------------------------ 2026-11-26 multiplayer (SessionOpenMP mod channel)
-- Each player sends its clip layers (ASN_NetPacket: which clip, clip time, weight) 15 times a second; the others play
-- the same clips on that player's skater (ASN_NetReceive, DLL). They need the same clip files (same name and content).
local NET = {
    CHANNEL = "animloader.clips1", SEND_EVERY = 1 / 15, SCAN_EVERY = 1.0, STATS_EVERY = 30.0,
    ch = nil, slots = {}, used = {}, sendT = 0, scanT = 0, statsT = 0, inSession = nil,
    sent = 0, got = 0, bad = 0, idle = 0, missTold = {},
}
local function npname(p) return tostring(q(function() return OpenMP.PlayerName(p) end, "?")) .. " (#" .. tostring(p) .. ")" end
local function netForget(p)
    local e = NET.slots[p]
    if not e then return end
    pcall(function() ASN_SetPeer(e.slot, 0) end)
    NET.used[e.slot], NET.slots[p] = nil, nil
end
local function netScan()
    local players = q(function() return OpenMP.Players(NET.ch) end, {}) or {}
    local seen = {}
    for _, p in ipairs(players) do
        seen[p] = true
        local actor = q(function() return OpenMP.PlayerActor(p) end, nil)
        local mesh = valid(actor) and q(function() return actor.Mesh end, nil) or nil
        local ma = valid(mesh) and addr(mesh) or nil
        local e = NET.slots[p]
        if not e then
            for s = 0, 3 do if NET.used[s] == nil then e = { slot = s } NET.slots[p], NET.used[s] = e, p break end end
            if not e and not NET.fullTold then NET.fullTold = true log("net: more than 4 players with Session Anim Loader, " .. npname(p) .. " ignored") end
        end
        if e and e.meshAddr ~= ma then
            e.meshAddr = ma
            pcall(function() ASN_SetPeer(e.slot, ma or 0) end)
            if ma then log(string.format("net: skater of %s found (mesh 0x%x) -> slot %d", npname(p), ma, e.slot))
            else log("net: skater of " .. npname(p) .. " not in the world (another map?)") end
        end
    end
    for p, _ in pairs(NET.slots) do if not seen[p] then netForget(p) end end
end
local function netOnMessage(p, data)
    if type(data) ~= "string" then return end
    if data:sub(1, 2) == "S1" then
        -- v0.19 / v0.20 named clips by file name: not compatible any more
        NET.oldTold = NET.oldTold or {}
        if not NET.oldTold[p] then
            NET.oldTold[p] = true
            log("net: " .. npname(p) .. " uses an older version of the mod (v0.20 or before): update both to see each other's clips")
        end
    elseif data:sub(1, 2) == "S2" then
        NET.got = NET.got + 1
        local e = NET.slots[p]
        if e then
            local r = tonumber(q(function() return ASN_NetReceive(e.slot, data) end, -1)) or -1
            if r == -1 then NET.bad = NET.bad + 1 end
        end
    elseif data:sub(1, 5) == "HELLO" then
        log("net: " .. npname(p) .. " says: " .. data)
    end
end
local function netOnPlayer(p, joined)
    if joined then
        log("net: " .. npname(p) .. " has Session Anim Loader")
        pcall(function() OpenMP.Send(NET.ch, p, "HELLO SessionAnimLoader " .. VERSION .. " DLL v0." .. tostring(q(function() return ASN_Version() end, "?")), true) end)
    else
        log("net: " .. npname(p) .. " left")
        netForget(p)
    end
end
local function netInit()
    if NET.ch or NET.off then return end
    if not OpenMP then NET.off = true log("net: SessionOpenMP absent, Session Anim Loader stays solo") return end
    if type(ASN_NetPacket) ~= "function" then NET.off = true log("net: the DLL has no network functions (v0.19+)") return end
    NET.ch = OpenMP.Register(NET.CHANNEL, function(p, d) local ok, er = pcall(netOnMessage, p, d) if not ok then log("net: error " .. tostring(er)) end end,
                                          function(p, j) local ok, er = pcall(netOnPlayer, p, j) if not ok then log("net: error " .. tostring(er)) end end)
    if not NET.ch then NET.off = true log("net: OpenMP refused the channel " .. NET.CHANNEL) return end
    log("net: channel " .. NET.CHANNEL .. " open (OpenMP API " .. tostring(OpenMP.ApiVersion) .. ")")
end
local function netTick(now)
    netInit()
    if not NET.ch then return end
    local inS = q(function() return OpenMP.InSession() end, false) == true
    if inS ~= NET.inSession then
        NET.inSession = inS
        log(inS and "net: in an OpenMP session, your clips are sent to the other players" or "net: not in a session")
    end
    if not inS then return end
    if now - NET.scanT >= NET.SCAN_EVERY then
        NET.scanT = now
        local ok, er = pcall(netScan)
        if not ok then log("net: error " .. tostring(er)) end
    end
    if now - NET.sendT >= NET.SEND_EVERY then
        NET.sendT = now
        local pkt = tostring(q(function() return ASN_NetPacket() end, ""))
        -- an empty frame is "S2" + 10 hex characters: send 3 of them after the last clip, then stay quiet
        if #pkt > 12 then NET.idle = 0 else NET.idle = NET.idle + 1 end
        if #pkt > 2 and NET.idle <= 3 and next(NET.slots) ~= nil then
            if q(function() return OpenMP.Send(NET.ch, OpenMP.EVERYONE, pkt, false) end, false) then NET.sent = NET.sent + 1 end
        end
    end
    if now - NET.statsT >= NET.STATS_EVERY then
        NET.statsT = now
        local parts = {}
        for p, e in pairs(NET.slots) do
            local st = tostring(q(function() return ASN_PeerStats(e.slot) end, "?"))
            parts[#parts + 1] = npname(p) .. " : " .. st
            local miss = tonumber(st:match("clips missing here (%d+)") or "0") or 0
            if miss > 0 and not NET.missTold[p] then
                NET.missTold[p] = true
                log("net: " .. npname(p) .. " plays a clip you do not have (or another version of it): their skater keeps the game animation for it. To see it, ask them for the .sanim file and put it in Mods\\SessionAnimLoader\\clips\\others\\ (any file name)")
            end
        end
        log(string.format("net: sent %d, received %d, refused %d | %s", NET.sent, NET.got, NET.bad,
            #parts > 0 and table.concat(parts, " ; ") or "no other player with Session Anim Loader"))
    end
end

local function frame(ctx)
    local sk = ctx:get()
    if not valid(sk) then return end
    local a = addr(sk)
    if a ~= P.skAddr or not valid(P.sk) then
        if q(function() return sk:IsPlayerControlled() end, false) ~= true then return end   -- not the co-op proxies
        P.sk, P.skAddr, P.adoptT = sk, a, os.clock()
        P.mesh, P.meshAddr, P.rigFor, P.inst = nil, nil, nil, nil
        P.deck, P.boardTries = nil, 0
        log("player skater adopted: " .. q(function() return sk:GetFName():ToString() end, "?"))
    end
    if not native() then
        if not P.told then P.told = true log("SessionAnimLoaderNative v0.5 missing (ASN_* functions not found): nothing will be applied") end
        return
    end
    if not P.configured then
        P.configured = true
        log(string.format("SessionAnimLoaderNative v%s present | Ctrl+F3 mod on/off, Ctrl+F4 capture", tostring(q(function() return ASN_Version() end, "?"))))
    end
    local now = os.clock()

    -- mesh and skeleton (2 s after adoption, then on every mesh or skeleton change)
    local mesh = q(function() return sk.Mesh end, nil)
    if not valid(mesh) then return end
    local ma = addr(mesh)
    local skm = q(function() return mesh.SkeletalMesh end, nil)
    local ska = valid(skm) and addr(skm) or nil
    if now - P.adoptT >= 2.0 and (ma ~= P.meshAddr or ska ~= P.rigFor) then
        P.mesh, P.meshAddr, P.rigFor, P.inst = mesh, ma, ska, nil
        local okr, er = pcall(sendRig)
        if not okr then log("bone table: error " .. tostring(er)) end
        local st = tonumber(q(function() return ASN_SetMesh(ma) end, -2)) or -2
        log(string.format("mesh sent (%s) -> state %d: %s", string.format("0x%x", ma or 0), st, tostring(q(function() return ASN_Why() end, "?"))))
        P.state = st
        scanClips()
        P.scanT = now
        P.deck, P.boardTries, P.boardT = nil, BOARD.TRIES, now - BOARD.EVERY   -- 2026-11-27: search the board again
    end
    if P.meshAddr == nil then return end
    boardSearch(sk, now)

    -- installation waiting (ExpandedTricks not there yet): retry every 0.5 s
    if P.state == 0 and now - P.pollT >= 0.5 then
        P.pollT = now
        local st = tonumber(q(function() return ASN_Poll() end, -2)) or -2
        if st ~= P.state then
            P.state = st
            log(string.format("installation -> state %d: %s", st, tostring(q(function() return ASN_Why() end, "?"))))
        end
    end

    -- game replay (editor or filmer camera): CustomMovementMode 6, the skater is replayed (ExpandedTricks v149)
    local replay = (tonumber(q(function() return sk.CharacterMovement.CustomMovementMode end, -1)) or -1) == 6
    if replay ~= P.replay then
        P.replay = replay
        log(replay and "game replay: mod standing by (the recorded pose already holds our clips)" or "replay ended: mod active")
    end

    -- game state -> C++ (every frame)
    local manual, nose, goofy, sw, ratio, kf, serial, phase, grind, pslide, grab, onBoard, grounded, crank, landing, footPos, speed, pushing = readState(now)
    if manual ~= nil then
        -- 2026-11-25: which grind (crooked, smith, feeble...): the skater's _replicatedGrindName, sent when it changes
        local gname = ""
        if grind then
            gname = tostring(q(function() return sk._replicatedGrindName:ToString() end, "") or "")
            if gname == "None" then gname = "" end
        end
        if gname ~= P.grindName then
            P.grindName = gname
            pcall(function() ASN_SetGrind(gname) end)
            if grind then
                local idx = tonumber(q(function() return P.inst.GrindAnimSetIndex end, -1)) or -1
                local lip = q(function() return P.inst.IsGrindingInLiptrick end, false) == true
                log(string.format("grind: name '%s', GrindAnimSetIndex %d, liptrick %s", gname, idx, tostring(lip)))
            end
        end
        local vz, dz, jok, popH = jumpState(sk)
        sendBoard()   -- 2026-11-27: just before ASN_SetState, which takes it
        ASN_SetState(b01(manual), b01(nose), b01(goofy), b01(sw), ratio, b01(kf), serial, phase, b01(grind), b01(pslide), b01(grab),
                     b01(onBoard), b01(grounded), b01(crank), b01(landing), footPos, speed, b01(pushing), b01(replay),
                     vz, dz, jok, popH)
        if grind ~= P.grind or pslide ~= P.powerslide or grab ~= P.grab then
            P.grind, P.powerslide, P.grab = grind, pslide, grab
            log(string.format("states: grind %s, powerslide %s, grab %s", tostring(grind), tostring(pslide), tostring(grab)))
        end
        noteState(manual, nose, goofy, sw, ratio)
    end

    -- clips folder
    if now - P.scanT >= cfg.ScanEvery then
        P.scanT = now
        scanClips()
    end

    -- 2026-11-26 multiplayer
    local okn, ern = pcall(netTick, now)
    if not okn and not NET.errTold then NET.errTold = true log("net: error " .. tostring(ern)) end

    -- keys
    if P.wantToggle then
        P.wantToggle = false
        P.on = not P.on
        ASN_Set("on", P.on and 1 or 0)
        stats(P.on and "loader ON" or "loader OFF")
    end
    if P.wantCapture then
        P.wantCapture = false
        if P.state ~= 1 then
            log("Ctrl+F4 ignored: the layer is not installed (state " .. tostring(P.state) .. ")")
        else
            ASN_Capture()
            P.capturePending, P.captureT = true, now
        end
    end
    if P.capturePending then
        local path = tostring(q(function() return ASN_TakeCapture() end, ""))
        if path ~= "" then
            P.capturePending = false
            log("capture written: " .. path)
        elseif now - P.captureT > 2.0 then
            P.capturePending = false
            log("capture: nothing received within 2 s (the pose did not go through the layer)")
        end
    end

    if now - P.statT >= cfg.StatsEvery then
        P.statT = now
        stats("compteurs")
    end
end

local okh, errh = pcall(function()
    RegisterHook(HOOK_FN, function(ctx)
        local ok, e = pcall(frame, ctx)
        if not ok and not P.errTold then P.errTold = true log("error in the hook: " .. tostring(e)) end
    end)
end)
log(okh and "hook registered" or ("hook failed: " .. tostring(errh)))

RegisterKeyBind(Key.F3, { ModifierKey.CONTROL }, function() P.wantToggle = true end)
RegisterKeyBind(Key.F4, { ModifierKey.CONTROL }, function() P.wantCapture = true end)

log(VERSION .. " loaded")
