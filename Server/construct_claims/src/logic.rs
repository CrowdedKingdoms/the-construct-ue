// Your server code for the Server Object type construct_claims. The generator writes this file once and never again.
use crate::*;
use ckx_sdk::node::Grid;
use ckx_sdk::prelude::*;
use std::cell::Cell;
use std::ops::Bound;

const CLAIMS_PER_PLAYER: usize = 64;
const CLAIMS_IN_ALL: usize = 10_000;
const LIST_DEFAULT: usize = 200;
const CHECKS_PER_SWEEP: usize = 64;
// Grid reads stop once a call has used this long: past the type's 2,000 ms deadline the instance crashes and loses its work.
const READ_BUDGET_MS: u64 = 1_000;

const NOT_YOUR_CLAIM: &str = "this grid is not your claim on that chunk";
const NOT_CHECKED: &str = "the claim could not be checked";

thread_local! {
    // The last grid id the registry sweep checked. Kept outside the state, so a refused call's rollback cannot rewind it.
    static SWEPT_TO: Cell<i64> = const { Cell::new(0) };
}

// The caller's user id as the int64 a claim stores; a developer or another Server Object is not a player.
fn calling_player(call: &Call<'_>) -> Option<i64> {
    call.player().ok().and_then(|player| i64::try_from(player).ok())
}

fn chunk_pos(chunk: &ConstructHubChunk) -> ChunkPos {
    ChunkPos::new(chunk.X, chunk.Y, chunk.Z)
}

// The grid read exposes no acquiredVia, so a one-chunk plot the player owns passes too.
fn is_claim_of(grid: &Grid, chunk: &ConstructHubChunk, owner: i64) -> bool {
    let at = chunk_pos(chunk);
    let owned = grid.owner.as_ref().is_some_and(|by| by.kind.eq_ignore_ascii_case("user") && by.ref_ == owner.to_string());
    grid.low == at && grid.high == at && owned
}

// Some(true) while the platform still holds the record's grid as that player's claim on that chunk; None when it could not say.
fn still_claimed(ctx: &Ctx, claim: &ConstructHubClaim) -> Option<bool> {
    match ctx.grids().get(claim.GridId as u64) {
        Ok(grid) => Some(grid.is_some_and(|grid| is_claim_of(&grid, &claim.Chunk, claim.OwnerUserId))),
        // A released grid can come back as NotFound rather than as no grid; both mean the claim is gone.
        Err(error) if error.status == Status::NotFound => Some(false),
        Err(_) => None,
    }
}

impl ConstructClaimsState {
    fn held_by(&self, owner: i64) -> usize {
        self.Claims.values().filter(|claim| claim.OwnerUserId == owner).count()
    }

    // Drops the player's records whose grid is gone or no longer theirs; refuses if they still hold the most a player may.
    fn make_room_for(&mut self, ctx: &Ctx, owner: i64, started: u64) -> Result<()> {
        let mut mine: Vec<ConstructHubClaim> = self.Claims.values().filter(|claim| claim.OwnerUserId == owner).cloned().collect();
        // Start at a clock-chosen record, so calls the read budget stops early still cover them all between them.
        let first = ctx.now_ms() as usize % mine.len().max(1);
        mine.rotate_left(first);
        let mut unchecked = false;
        for claim in &mine {
            if ctx.now_ms().saturating_sub(started) >= READ_BUDGET_MS {
                unchecked = true;
                break;
            }
            match still_claimed(ctx, claim) {
                Some(false) => {
                    self.Claims.remove(&claim.GridId);
                }
                Some(true) => {}
                None => unchecked = true,
            }
        }
        if self.held_by(owner) < CLAIMS_PER_PLAYER {
            return Ok(());
        }
        Err(Error::new(if unchecked { NOT_CHECKED } else { "a player keeps at most 64 claims" }))
    }

    // Checks the next records after the last one swept, wrapping at the end, and drops each the platform no longer holds.
    fn sweep(&mut self, ctx: &Ctx, started: u64) {
        let after = match SWEPT_TO.get() {
            // A fresh instance has no position; starting from the clock keeps a restart from rechecking the oldest records.
            0 => match ctx.now_ms() as usize % self.Claims.len().max(1) {
                0 => 0,
                start => self.Claims.keys().nth(start - 1).copied().unwrap_or(0),
            },
            swept => swept,
        };
        let later = self.Claims.range((Bound::Excluded(after), Bound::Unbounded));
        let batch: Vec<ConstructHubClaim> = later.chain(self.Claims.range(..=after)).take(CHECKS_PER_SWEEP).map(|(_, claim)| claim.clone()).collect();
        for claim in &batch {
            if ctx.now_ms().saturating_sub(started) >= READ_BUDGET_MS {
                break;
            }
            if still_claimed(ctx, claim) == Some(false) {
                self.Claims.remove(&claim.GridId);
            }
            // Saved per record checked, so a pass the read budget stops still moves the sweep on.
            SWEPT_TO.set(claim.GridId);
        }
    }
}

impl Functions for ConstructClaimsState {
    fn list_claims(&mut self, _ctx: &Ctx, call: &Call<'_>, params: ListClaimsParams) -> Result<ListClaimsReply> {
        let mine = if params.bOnlyMine {
            Some(calling_player(call).ok_or_else(|| Error::new("only a player has claims of their own"))?)
        } else {
            None
        };
        let limit = if params.Limit == 0 { LIST_DEFAULT } else { params.Limit as usize };
        let Claims = self
            .Claims
            .values()
            .filter(|claim| !params.bOnlyChunk || claim.Chunk == params.Chunk)
            .filter(|claim| mine.map_or(true, |owner| claim.OwnerUserId == owner))
            .take(limit)
            .cloned()
            .collect();
        Ok(ListClaimsReply { Claims, Total: self.Claims.len() as i32 })
    }

    fn record_claim(&mut self, ctx: &Ctx, call: &Call<'_>, params: RecordClaimParams) -> Result<RecordClaimReply> {
        let started = ctx.now_ms();
        let owner = calling_player(call).ok_or_else(|| Error::new("a player must call this"))?;
        if params.GridId < 1 {
            return Err(Error::new("the grid id must be positive"));
        }
        let smallest = ctx.grids().at(chunk_pos(&params.Chunk), GridPick::Smallest).map_err(|_| Error::new(NOT_CHECKED))?;
        if !smallest.is_some_and(|grid| grid.id == params.GridId as u64 && is_claim_of(&grid, &params.Chunk, owner)) {
            return Err(Error::new(NOT_YOUR_CLAIM));
        }
        if let Some(claim) = self.Claims.get(&params.GridId).filter(|claim| claim.OwnerUserId == owner && claim.Chunk == params.Chunk) {
            return Ok(RecordClaimReply { bRecorded: false, Claim: claim.clone() });
        }
        // The platform has just said the caller owns this chunk, so any other record of the grid or on the chunk is stale.
        // A refusal below rolls this back: a failed call leaves the state as it was.
        self.Claims.retain(|grid, claim| *grid != params.GridId && claim.Chunk != params.Chunk);
        if self.held_by(owner) >= CLAIMS_PER_PLAYER {
            self.make_room_for(ctx, owner, started)?;
        }
        if self.Claims.len() >= CLAIMS_IN_ALL {
            self.sweep(ctx, started);
        }
        if self.Claims.len() >= CLAIMS_IN_ALL {
            return Err(Error::new("the claim registry is full"));
        }
        let Claim = ConstructHubClaim { GridId: params.GridId, Chunk: params.Chunk, OwnerUserId: owner, ClaimedAt: ctx.now_ms() as i64 };
        self.Claims.insert(params.GridId, Claim.clone());
        ctx.persist_now();
        Ok(RecordClaimReply { bRecorded: true, Claim })
    }

    fn release_claim(&mut self, ctx: &Ctx, call: &Call<'_>, params: ReleaseClaimParams) -> Result<ReleaseClaimReply> {
        let owner = calling_player(call).ok_or_else(|| Error::new("a player must call this"))?;
        let Some(claim) = self.Claims.get(&params.GridId) else {
            return Ok(ReleaseClaimReply { bReleased: false });
        };
        if claim.OwnerUserId != owner {
            return Err(Error::new("only the claim's owner may release it"));
        }
        self.Claims.remove(&params.GridId);
        ctx.persist_now();
        Ok(ReleaseClaimReply { bReleased: true })
    }

    fn forget_claim(&mut self, ctx: &Ctx, _call: &Call<'_>, params: ForgetClaimParams) -> Result<ForgetClaimReply> {
        let bForgotten = self.Claims.remove(&params.GridId).is_some();
        if bForgotten {
            ctx.log(Level::Info, "a developer forgot a claim");
            ctx.persist_now();
        }
        Ok(ForgetClaimReply { bForgotten })
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use ckx_sdk::testing::TestHub;
    use ckx_sdk::{CallError, Status};
    use std::cell::RefCell;
    use std::collections::BTreeMap;
    use std::rc::Rc;

    const A: Caller = Caller::Player(101);
    const B: Caller = Caller::Player(202);
    const DEVELOPER: Caller = Caller::Developer(9);
    const A_ID: i64 = 101;
    const B_ID: i64 = 202;
    const WAIT_MS: u64 = 1_000;
    const WORLD_GRID: i64 = 900_000_000;

    // One platform grid, as the stand-in for the grid read holds it.
    #[derive(Clone)]
    struct PlatformGrid {
        low: (i64, i64, i64),
        high: (i64, i64, i64),
        owner: Option<i64>,
    }

    impl PlatformGrid {
        fn covers(&self, at: (i64, i64, i64)) -> bool {
            (self.low.0..=self.high.0).contains(&at.0) && (self.low.1..=self.high.1).contains(&at.1) && (self.low.2..=self.high.2).contains(&at.2)
        }

        fn volume(&self) -> i128 {
            let side = |low: i64, high: i64| (high as i128 - low as i128) + 1;
            side(self.low.0, self.high.0) * side(self.low.1, self.high.1) * side(self.low.2, self.high.2)
        }
    }

    // The platform's grids as the hub reads them: by id, or the smallest over a chunk, with the app's world grid under all.
    #[derive(Clone, Default)]
    struct Platform {
        grids: Rc<RefCell<BTreeMap<i64, PlatformGrid>>>,
        // Every read fails, or only the reads by grid id.
        down: Rc<Cell<bool>>,
        gets_down: Rc<Cell<bool>>,
        // A read by id of a grid that is gone answers NotFound instead of no grid.
        gone_as_not_found: Rc<Cell<bool>>,
    }

    impl Platform {
        fn claim(&self, grid: i64, x: i64, owner: i64) {
            self.grids.borrow_mut().insert(grid, PlatformGrid { low: (x, 0, 0), high: (x, 0, 0), owner: Some(owner) });
        }

        fn remove(&self, grid: i64) {
            self.grids.borrow_mut().remove(&grid);
        }

        fn number(text: Option<&str>) -> i64 {
            text.and_then(|digits| digits.parse().ok()).unwrap_or(i64::MIN)
        }

        fn find(&self, grid: Option<&str>, at: (i64, i64, i64)) -> Option<(i64, PlatformGrid)> {
            let grids = self.grids.borrow();
            if grid.is_some() {
                let id = Self::number(grid);
                return grids.get(&id).map(|found| (id, found.clone()));
            }
            grids.iter().filter(|(_, found)| found.covers(at)).min_by_key(|(id, found)| (found.volume(), **id)).map(|(id, found)| (*id, found.clone()))
        }
    }

    fn hub_on(platform: &Platform) -> TestHub<ServerObject> {
        let hub = TestHub::spawn("construct_claims", "main").unwrap();
        let huge = 1_000_000_000_000;
        platform.grids.borrow_mut().insert(WORLD_GRID, PlatformGrid { low: (-huge, -huge, -huge), high: (huge, huge, huge), owner: None });
        let answers = platform.clone();
        hub.answer_node("grids.get", move |request| {
            if answers.down.get() || (answers.gets_down.get() && request["grid"].as_str().is_some()) {
                return Err(CallError { status: Status::RateLimited, message: String::from("the platform is unavailable") });
            }
            let at = &request["at"];
            let found = answers.find(request["grid"].as_str(), (Platform::number(at["x"].as_str()), Platform::number(at["y"].as_str()), Platform::number(at["z"].as_str())));
            let mut reply = request.clone();
            reply.take();
            reply["grid"] = Default::default();
            let Some((id, grid)) = found else {
                if answers.gone_as_not_found.get() && request["grid"].as_str().is_some() {
                    return Err(CallError { status: Status::NotFound, message: String::from("no such grid") });
                }
                return Ok(reply);
            };
            reply["grid"]["gridId"] = id.to_string().into();
            for (name, corner) in [("low", grid.low), ("high", grid.high)] {
                reply["grid"][name]["x"] = corner.0.to_string().into();
                reply["grid"][name]["y"] = corner.1.to_string().into();
                reply["grid"][name]["z"] = corner.2.to_string().into();
            }
            if let Some(owner) = grid.owner {
                reply["grid"]["owner"]["kind"] = "USER".into();
                reply["grid"]["owner"]["ref"] = owner.to_string().into();
            }
            Ok(reply)
        });
        hub
    }

    fn setup() -> (TestHub<ServerObject>, Platform) {
        let platform = Platform::default();
        (hub_on(&platform), platform)
    }

    fn chunk(x: i64) -> ConstructHubChunk {
        ConstructHubChunk { X: x, Y: 0, Z: 0 }
    }

    fn record_params(grid: i64, x: i64) -> RecordClaimParams {
        RecordClaimParams { GridId: grid, Chunk: chunk(x) }
    }

    fn record(hub: &mut TestHub<ServerObject>, caller: Caller, grid: i64, x: i64) -> Result<RecordClaimReply, CallError> {
        let reply = hub.call_typed(caller, "record_claim", &record_params(grid, x));
        hub.advance(WAIT_MS).unwrap();
        reply
    }

    // The platform claim and its record, as a player who claimed and then recorded makes them.
    fn claim_and_record(hub: &mut TestHub<ServerObject>, platform: &Platform, caller: Caller, owner: i64, grid: i64, x: i64) -> RecordClaimReply {
        platform.claim(grid, x, owner);
        record(hub, caller, grid, x).unwrap()
    }

    fn release(hub: &mut TestHub<ServerObject>, caller: Caller, grid: i64) -> Result<ReleaseClaimReply, CallError> {
        let reply = hub.call_typed(caller, "release_claim", &ReleaseClaimParams { GridId: grid });
        hub.advance(WAIT_MS).unwrap();
        reply
    }

    fn list(hub: &mut TestHub<ServerObject>, caller: Caller, params: ListClaimsParams) -> Result<ListClaimsReply, CallError> {
        let reply = hub.call_typed(caller, "list_claims", &params);
        hub.advance(WAIT_MS).unwrap();
        reply
    }

    fn refusal<T: std::fmt::Debug>(reply: Result<T, CallError>) -> String {
        let error = reply.expect_err("the call should be refused");
        assert_eq!(error.status, Status::AppError);
        error.message
    }

    // Writes a record straight into the state, as an earlier version of the hub could have left it.
    fn stored(hub: &mut TestHub<ServerObject>, grid: i64, x: i64, owner: i64) {
        let claim = ConstructHubClaim { GridId: grid, Chunk: chunk(x), OwnerUserId: owner, ClaimedAt: 1 };
        hub.hub_mut().state.Claims.insert(grid, claim);
    }

    fn recorded(hub: &TestHub<ServerObject>) -> Vec<(i64, i64, i64)> {
        hub.hub().state.Claims.values().map(|claim| (claim.GridId, claim.Chunk.X, claim.OwnerUserId)).collect()
    }

    fn reads(hub: &TestHub<ServerObject>) -> usize {
        hub.node_requests().iter().filter(|(op, _)| op == "grids.get").count()
    }

    #[test]
    fn a_checked_claim_is_stored_with_the_caller_as_owner_and_saved() {
        let (mut hub, platform) = setup();
        let reply = claim_and_record(&mut hub, &platform, A, A_ID, 5, 1);
        assert!(reply.bRecorded);
        assert_eq!(reply.Claim.GridId, 5);
        assert_eq!(reply.Claim.Chunk, chunk(1));
        assert_eq!(reply.Claim.OwnerUserId, A_ID);
        assert!(reply.Claim.ClaimedAt > 0);
        assert_eq!(hub.persist_asked(), 1);
    }

    #[test]
    fn a_grid_id_below_one_is_refused_before_any_read_and_one_is_not() {
        let (mut hub, platform) = setup();
        assert_eq!(refusal(record(&mut hub, A, 0, 1)), "the grid id must be positive");
        assert_eq!(refusal(record(&mut hub, A, -3, 1)), "the grid id must be positive");
        assert_eq!(reads(&hub), 0);
        assert!(claim_and_record(&mut hub, &platform, A, A_ID, 1, 1).bRecorded);
    }

    #[test]
    fn the_record_reads_only_the_smallest_grid_over_its_chunk() {
        let (mut hub, platform) = setup();
        claim_and_record(&mut hub, &platform, A, A_ID, 5, 1);
        let requests = hub.node_requests();
        assert_eq!(requests.len(), 1);
        assert_eq!(requests[0].1["pick"].as_str(), Some("smallest"));
        assert_eq!(requests[0].1["at"]["x"].as_str(), Some("1"));
    }

    #[test]
    fn a_made_up_grid_is_refused_and_the_real_claim_is_not() {
        let (mut hub, platform) = setup();
        assert_eq!(refusal(record(&mut hub, A, 5, 1)), NOT_YOUR_CLAIM);
        platform.claim(5, 1, A_ID);
        assert!(record(&mut hub, A, 5, 1).unwrap().bRecorded);
    }

    #[test]
    fn the_world_grid_is_not_a_claim() {
        let (mut hub, _platform) = setup();
        assert_eq!(refusal(record(&mut hub, A, WORLD_GRID, 1)), NOT_YOUR_CLAIM);
    }

    #[test]
    fn a_grid_other_than_the_one_over_the_chunk_is_refused() {
        let (mut hub, platform) = setup();
        platform.claim(5, 1, A_ID);
        platform.claim(6, 2, A_ID);
        assert_eq!(refusal(record(&mut hub, A, 6, 1)), NOT_YOUR_CLAIM);
        assert!(record(&mut hub, A, 6, 2).unwrap().bRecorded);
    }

    #[test]
    fn another_players_claim_is_refused_and_their_own_record_is_not() {
        let (mut hub, platform) = setup();
        platform.claim(5, 1, A_ID);
        assert_eq!(refusal(record(&mut hub, B, 5, 1)), NOT_YOUR_CLAIM);
        assert!(record(&mut hub, A, 5, 1).unwrap().bRecorded);
    }

    #[test]
    fn a_grid_wider_than_the_chunk_is_refused() {
        let (mut hub, platform) = setup();
        platform.grids.borrow_mut().insert(5, PlatformGrid { low: (1, 0, 0), high: (2, 0, 0), owner: Some(A_ID) });
        assert_eq!(refusal(record(&mut hub, A, 5, 1)), NOT_YOUR_CLAIM);
        platform.claim(6, 1, A_ID);
        assert!(record(&mut hub, A, 6, 1).unwrap().bRecorded);
    }

    #[test]
    fn a_grid_with_no_owner_is_refused() {
        let (mut hub, platform) = setup();
        platform.grids.borrow_mut().insert(5, PlatformGrid { low: (1, 0, 0), high: (1, 0, 0), owner: None });
        assert_eq!(refusal(record(&mut hub, A, 5, 1)), NOT_YOUR_CLAIM);
        platform.claim(5, 1, A_ID);
        assert!(record(&mut hub, A, 5, 1).unwrap().bRecorded);
    }

    #[test]
    fn a_one_chunk_plot_the_caller_owns_is_accepted() {
        // A Studio plot over one chunk with a user owner reads the same as a claim: the grid read carries no acquiredVia.
        let (mut hub, platform) = setup();
        platform.grids.borrow_mut().insert(7, PlatformGrid { low: (3, 0, 0), high: (3, 0, 0), owner: Some(A_ID) });
        assert!(record(&mut hub, A, 7, 3).unwrap().bRecorded);
    }

    #[test]
    fn a_failed_read_is_refused_as_unchecked() {
        let (mut hub, platform) = setup();
        platform.claim(5, 1, A_ID);
        platform.down.set(true);
        assert_eq!(refusal(record(&mut hub, A, 5, 1)), NOT_CHECKED);
        platform.down.set(false);
        assert!(record(&mut hub, A, 5, 1).unwrap().bRecorded);
    }

    #[test]
    fn neither_check_refusal_starts_the_cooldown() {
        let (mut hub, platform) = setup();
        assert_eq!(refusal(hub.call_typed::<_, RecordClaimReply>(A, "record_claim", &record_params(5, 1))), NOT_YOUR_CLAIM);
        platform.claim(5, 1, A_ID);
        platform.down.set(true);
        assert_eq!(refusal(hub.call_typed::<_, RecordClaimReply>(A, "record_claim", &record_params(5, 1))), NOT_CHECKED);
        platform.down.set(false);
        assert!(hub.call_typed::<_, RecordClaimReply>(A, "record_claim", &record_params(5, 1)).unwrap().bRecorded);
    }

    #[test]
    fn recording_a_grid_the_caller_holds_answers_unchanged() {
        let (mut hub, platform) = setup();
        let first = claim_and_record(&mut hub, &platform, A, A_ID, 5, 1);
        let again = record(&mut hub, A, 5, 1).unwrap();
        assert!(!again.bRecorded);
        assert_eq!(again.Claim, first.Claim);
        assert_eq!(hub.persist_asked(), 1);
    }

    #[test]
    fn a_stale_record_of_the_grid_is_replaced() {
        // Another player's record of the grid outlived their claim, and the grid id came back to A.
        let (mut hub, platform) = setup();
        stored(&mut hub, 5, 1, B_ID);
        platform.claim(5, 1, A_ID);
        assert!(record(&mut hub, A, 5, 1).unwrap().bRecorded);
        assert_eq!(recorded(&hub), [(5, 1, A_ID)]);
    }

    #[test]
    fn a_stale_record_on_the_chunk_is_replaced() {
        // B released the chunk without telling the hub, and A has claimed it since under a new grid.
        let (mut hub, platform) = setup();
        claim_and_record(&mut hub, &platform, B, B_ID, 5, 1);
        platform.remove(5);
        platform.claim(6, 1, A_ID);
        assert!(record(&mut hub, A, 6, 1).unwrap().bRecorded);
        assert_eq!(recorded(&hub), [(6, 1, A_ID)]);
    }

    #[test]
    fn a_new_grid_replaces_the_callers_own_on_that_chunk() {
        let (mut hub, platform) = setup();
        claim_and_record(&mut hub, &platform, A, A_ID, 5, 1);
        platform.remove(5);
        platform.claim(6, 1, A_ID);
        assert!(record(&mut hub, A, 6, 1).unwrap().bRecorded);
        assert_eq!(recorded(&hub), [(6, 1, A_ID)]);
    }

    #[test]
    fn a_record_of_the_grid_on_another_chunk_is_replaced() {
        let (mut hub, platform) = setup();
        stored(&mut hub, 5, 9, A_ID);
        platform.claim(5, 1, A_ID);
        assert!(record(&mut hub, A, 5, 1).unwrap().bRecorded);
        assert_eq!(recorded(&hub), [(5, 1, A_ID)]);
    }

    #[test]
    fn a_65th_live_claim_is_refused_until_one_is_released() {
        let (mut hub, platform) = setup();
        for grid in 1..=64 {
            claim_and_record(&mut hub, &platform, A, A_ID, grid, grid);
        }
        platform.claim(65, 65, A_ID);
        hub.clear();
        assert_eq!(refusal(record(&mut hub, A, 65, 65)), "a player keeps at most 64 claims");
        assert_eq!(reads(&hub), 1 + 64, "the cap re-reads each of the player's records by grid id");
        assert!(claim_and_record(&mut hub, &platform, B, B_ID, 66, 66).bRecorded);
        release(&mut hub, A, 1).unwrap();
        assert!(record(&mut hub, A, 65, 65).unwrap().bRecorded);
    }

    #[test]
    fn the_player_cap_drops_records_whose_grid_is_gone() {
        let (mut hub, platform) = setup();
        for grid in 1..=64 {
            claim_and_record(&mut hub, &platform, A, A_ID, grid, grid);
        }
        platform.remove(10);
        platform.claim(65, 65, A_ID);
        assert!(record(&mut hub, A, 65, 65).unwrap().bRecorded);
        let held = recorded(&hub);
        assert_eq!(held.len(), 64);
        assert!(!held.iter().any(|(grid, _, _)| *grid == 10));
    }

    #[test]
    fn the_player_cap_drops_records_no_longer_theirs() {
        let (mut hub, platform) = setup();
        for grid in 1..=64 {
            claim_and_record(&mut hub, &platform, A, A_ID, grid, grid);
        }
        platform.claim(10, 10, B_ID);
        platform.claim(65, 65, A_ID);
        assert!(record(&mut hub, A, 65, 65).unwrap().bRecorded);
        assert!(!recorded(&hub).iter().any(|(grid, _, _)| *grid == 10));
    }

    #[test]
    fn the_player_cap_drops_a_record_whose_grid_reads_as_not_found() {
        let (mut hub, platform) = setup();
        for grid in 1..=64 {
            claim_and_record(&mut hub, &platform, A, A_ID, grid, grid);
        }
        platform.remove(10);
        platform.gone_as_not_found.set(true);
        platform.claim(65, 65, A_ID);
        assert!(record(&mut hub, A, 65, 65).unwrap().bRecorded);
        assert!(!recorded(&hub).iter().any(|(grid, _, _)| *grid == 10));
    }

    #[test]
    fn the_sweep_drops_records_whose_grids_read_as_not_found() {
        let (mut hub, platform) = setup();
        fill_registry(&mut hub, &platform, |grid| grid > 64);
        platform.gone_as_not_found.set(true);
        platform.claim(20_000, 20_000, A_ID);
        assert!(record(&mut hub, A, 20_000, 20_000).unwrap().bRecorded);
        assert!(!(1..=64).any(|grid| hub.hub().state.Claims.contains_key(&grid)));
    }

    #[test]
    fn a_fresh_instance_starts_its_sweep_from_the_clock_not_the_oldest_records() {
        let (mut hub, platform) = setup();
        // The oldest 64 are live and everything after them stale: starting at the oldest would only refuse.
        fill_registry(&mut hub, &platform, |grid| grid <= 64);
        platform.claim(20_000, 20_000, A_ID);
        SWEPT_TO.set(0);
        let len = CLAIMS_IN_ALL as u64;
        let at = hub.now_ms() - hub.now_ms() % len + len + 5_000;
        hub.advance_to(at).unwrap();
        assert!(record(&mut hub, A, 20_000, 20_000).unwrap().bRecorded);
        assert!((1..=64).all(|grid| hub.hub().state.Claims.contains_key(&grid)), "the live oldest records were not the batch");
    }

    #[test]
    fn the_player_cap_refuses_as_unchecked_when_a_re_read_fails() {
        let (mut hub, platform) = setup();
        for grid in 1..=64 {
            claim_and_record(&mut hub, &platform, A, A_ID, grid, grid);
        }
        platform.claim(65, 65, A_ID);
        platform.remove(10);
        platform.gets_down.set(true);
        assert_eq!(refusal(record(&mut hub, A, 65, 65)), NOT_CHECKED);
        assert_eq!(recorded(&hub).len(), 64);
        platform.gets_down.set(false);
        assert!(record(&mut hub, A, 65, 65).unwrap().bRecorded);
    }

    #[test]
    fn replacing_a_claim_at_the_cap_is_allowed() {
        let (mut hub, platform) = setup();
        for grid in 1..=64 {
            claim_and_record(&mut hub, &platform, A, A_ID, grid, grid);
        }
        platform.remove(1);
        platform.claim(100, 1, A_ID);
        hub.clear();
        assert!(record(&mut hub, A, 100, 1).unwrap().bRecorded);
        assert_eq!(reads(&hub), 1, "replacing a record on the same chunk leaves room without re-reading");
        assert_eq!(recorded(&hub).len(), 64);
    }

    fn fill_registry(hub: &mut TestHub<ServerObject>, platform: &Platform, live: impl Fn(i64) -> bool) {
        for grid in 1..=CLAIMS_IN_ALL as i64 {
            let owner = grid + 1000;
            stored(hub, grid, grid, owner);
            if live(grid) {
                platform.claim(grid, grid, owner);
            }
        }
    }

    #[test]
    fn a_full_registry_of_live_claims_is_refused() {
        let (mut hub, platform) = setup();
        fill_registry(&mut hub, &platform, |_| true);
        platform.claim(20_000, 20_000, A_ID);
        hub.clear();
        assert_eq!(refusal(record(&mut hub, A, 20_000, 20_000)), "the claim registry is full");
        assert_eq!(reads(&hub), 1 + CHECKS_PER_SWEEP, "one sweep checks at most 64 records");
        hub.hub_mut().state.Claims.remove(&1);
        assert!(record(&mut hub, A, 20_000, 20_000).unwrap().bRecorded);
    }

    #[test]
    fn the_sweep_reaches_stale_records_past_64_live_ones_across_refusals() {
        let (mut hub, platform) = setup();
        fill_registry(&mut hub, &platform, |grid| grid <= 64);
        platform.claim(20_000, 20_000, A_ID);
        assert_eq!(refusal(record(&mut hub, A, 20_000, 20_000)), "the claim registry is full");
        assert_eq!(hub.hub().state.Claims.len(), CLAIMS_IN_ALL, "the refusal's rollback restores the records");
        let reply = record(&mut hub, A, 20_000, 20_000).unwrap();
        assert!(reply.bRecorded);
        let claims = &hub.hub().state.Claims;
        assert_eq!(claims.len(), CLAIMS_IN_ALL - CHECKS_PER_SWEEP + 1);
        assert!((1..=64).all(|grid| claims.contains_key(&grid)), "live records stay");
        assert!(!(65..=128).any(|grid| claims.contains_key(&grid)), "the second sweep dropped the next 64, all stale");
    }

    #[test]
    fn the_sweep_eventually_visits_the_whole_registry() {
        let (mut hub, platform) = setup();
        let last = CLAIMS_IN_ALL as i64;
        fill_registry(&mut hub, &platform, |grid| grid != last);
        platform.claim(20_000, 20_000, A_ID);
        let sweeps = CLAIMS_IN_ALL.div_ceil(CHECKS_PER_SWEEP);
        for _ in 1..sweeps {
            assert_eq!(refusal(record(&mut hub, A, 20_000, 20_000)), "the claim registry is full");
        }
        assert!(record(&mut hub, A, 20_000, 20_000).unwrap().bRecorded);
        assert!(!hub.hub().state.Claims.contains_key(&last));
    }

    #[test]
    fn the_sweep_keeps_a_record_it_could_not_check() {
        let (mut hub, platform) = setup();
        fill_registry(&mut hub, &platform, |grid| grid > 64);
        platform.claim(20_000, 20_000, A_ID);
        platform.gets_down.set(true);
        hub.clear();
        assert_eq!(refusal(record(&mut hub, A, 20_000, 20_000)), "the claim registry is full");
        assert_eq!(reads(&hub), 1 + CHECKS_PER_SWEEP);
        platform.gets_down.set(false);
        assert_eq!(refusal(record(&mut hub, A, 20_000, 20_000)), "the claim registry is full", "the unchecked 64 were passed over");
    }

    #[test]
    fn a_refusal_after_the_drop_keeps_the_callers_claim() {
        let (mut hub, platform) = setup();
        for grid in 1..=64 {
            claim_and_record(&mut hub, &platform, A, A_ID, grid, grid);
        }
        stored(&mut hub, 200, 70, B_ID);
        platform.claim(300, 70, A_ID);
        assert_eq!(refusal(record(&mut hub, A, 300, 70)), "a player keeps at most 64 claims");
        assert!(hub.hub().state.Claims.contains_key(&200), "the stale record on the chunk comes back with the rollback");
    }

    #[test]
    fn a_developer_cannot_record_or_release() {
        let (mut hub, platform) = setup();
        claim_and_record(&mut hub, &platform, A, A_ID, 5, 1);
        assert_eq!(refusal(hub.call_typed::<_, RecordClaimReply>(DEVELOPER, "record_claim", &record_params(6, 2))), "a player must call this");
        assert_eq!(refusal(hub.call_typed::<_, ReleaseClaimReply>(DEVELOPER, "release_claim", &ReleaseClaimParams { GridId: 5 })), "a player must call this");
        assert!(release(&mut hub, A, 5).unwrap().bReleased);
    }

    #[test]
    fn only_the_owner_releases_an_unknown_grid_answers_false_and_nothing_is_read() {
        let (mut hub, platform) = setup();
        claim_and_record(&mut hub, &platform, A, A_ID, 5, 1);
        hub.clear();
        assert_eq!(refusal(release(&mut hub, B, 5)), "only the claim's owner may release it");
        assert!(!release(&mut hub, A, 99).unwrap().bReleased);
        assert!(release(&mut hub, A, 5).unwrap().bReleased);
        assert_eq!(hub.persist_asked(), 1);
        assert_eq!(reads(&hub), 0, "a release never reads the platform");
    }

    #[test]
    fn only_a_developer_forgets_and_it_logs_no_id() {
        let (mut hub, platform) = setup();
        claim_and_record(&mut hub, &platform, A, A_ID, 5_123, 1);
        let denied = refusal(hub.call_typed::<_, ForgetClaimReply>(A, "forget_claim", &ReleaseClaimParams { GridId: 5_123 }));
        assert!(denied.starts_with("denied:"), "{denied}");
        let reply: ForgetClaimReply = hub.call_typed(DEVELOPER, "forget_claim", &ReleaseClaimParams { GridId: 5_123 }).unwrap();
        assert!(reply.bForgotten);
        assert_eq!(hub.persist_asked(), 2);
        let again: ForgetClaimReply = hub.call_typed(DEVELOPER, "forget_claim", &ReleaseClaimParams { GridId: 5_123 }).unwrap();
        assert!(!again.bForgotten);
        assert_eq!(hub.persist_asked(), 2);
        let logs = hub.logs();
        assert!(logs.iter().any(|line| line.text == "a developer forgot a claim"));
        assert!(logs.iter().all(|line| !line.text.contains("5123") && !line.text.contains("101")));
    }

    #[test]
    fn a_list_filters_orders_and_limits() {
        let (mut hub, platform) = setup();
        claim_and_record(&mut hub, &platform, A, A_ID, 30, 3);
        claim_and_record(&mut hub, &platform, B, B_ID, 10, 1);
        claim_and_record(&mut hub, &platform, A, A_ID, 20, 2);

        let all = list(&mut hub, A, ListClaimsParams::default()).unwrap();
        assert_eq!(all.Claims.iter().map(|claim| claim.GridId).collect::<Vec<_>>(), [10, 20, 30]);
        assert_eq!(all.Total, 3);

        let mine = list(&mut hub, A, ListClaimsParams { bOnlyMine: true, ..Default::default() }).unwrap();
        assert_eq!(mine.Claims.iter().map(|claim| claim.GridId).collect::<Vec<_>>(), [20, 30]);
        assert_eq!(mine.Total, 3);

        let at = list(&mut hub, B, ListClaimsParams { bOnlyChunk: true, Chunk: chunk(2), ..Default::default() }).unwrap();
        assert_eq!(at.Claims.iter().map(|claim| claim.GridId).collect::<Vec<_>>(), [20]);

        let limited = list(&mut hub, A, ListClaimsParams { Limit: 2, ..Default::default() }).unwrap();
        assert_eq!(limited.Claims.iter().map(|claim| claim.GridId).collect::<Vec<_>>(), [10, 20]);
    }

    #[test]
    fn a_limit_of_zero_means_200_and_past_1000_is_refused() {
        let (mut hub, _platform) = setup();
        for grid in 1..=250 {
            stored(&mut hub, grid, grid, A_ID);
        }
        assert_eq!(list(&mut hub, A, ListClaimsParams::default()).unwrap().Claims.len(), 200);
        assert_eq!(list(&mut hub, A, ListClaimsParams { Limit: 1000, ..Default::default() }).unwrap().Claims.len(), 250);
        assert!(refusal(list(&mut hub, A, ListClaimsParams { Limit: 1001, ..Default::default() })).starts_with("bad_params:"));
    }

    #[test]
    fn only_mine_from_a_developer_is_refused_and_a_plain_list_is_not() {
        let (mut hub, _platform) = setup();
        let refused = hub.call_typed::<_, ListClaimsReply>(DEVELOPER, "list_claims", &ListClaimsParams { bOnlyMine: true, ..Default::default() });
        assert_eq!(refusal(refused), "only a player has claims of their own");
        assert_eq!(hub.call_typed::<_, ListClaimsReply>(DEVELOPER, "list_claims", &ListClaimsParams::default()).unwrap().Total, 0);
    }

    #[test]
    fn the_cooldown_refuses_inside_a_second_and_a_refusal_does_not_start_it() {
        let (mut hub, platform) = setup();
        platform.claim(5, 1, A_ID);
        assert!(hub.call_typed::<_, RecordClaimReply>(A, "record_claim", &record_params(5, 1)).unwrap().bRecorded);
        let early = refusal(hub.call_typed::<_, RecordClaimReply>(A, "record_claim", &record_params(5, 1)));
        assert!(early.starts_with("cooldown:"), "{early}");
        hub.advance(WAIT_MS).unwrap();
        assert!(!hub.call_typed::<_, RecordClaimReply>(A, "record_claim", &record_params(5, 1)).unwrap().bRecorded);
        hub.advance(WAIT_MS).unwrap();

        refusal(hub.call_typed::<_, ReleaseClaimReply>(B, "release_claim", &ReleaseClaimParams { GridId: 5 }));
        assert!(!hub.call_typed::<_, ReleaseClaimReply>(B, "release_claim", &ReleaseClaimParams { GridId: 99 }).unwrap().bReleased);
    }

    #[test]
    fn listing_has_its_own_cooldown() {
        let (mut hub, _platform) = setup();
        let params = ListClaimsParams::default();
        assert!(hub.call_typed::<_, ListClaimsReply>(A, "list_claims", &params).is_ok());
        let early = refusal(hub.call_typed::<_, ListClaimsReply>(A, "list_claims", &params));
        assert!(early.starts_with("cooldown:"), "{early}");
        assert!(hub.call_typed::<_, ListClaimsReply>(B, "list_claims", &params).is_ok());
        hub.advance(WAIT_MS).unwrap();
        assert!(hub.call_typed::<_, ListClaimsReply>(A, "list_claims", &params).is_ok());
    }

    #[test]
    fn releasing_has_its_own_cooldown() {
        let (mut hub, _platform) = setup();
        let params = ReleaseClaimParams { GridId: 99 };
        assert!(hub.call_typed::<_, ReleaseClaimReply>(A, "release_claim", &params).is_ok());
        let early = refusal(hub.call_typed::<_, ReleaseClaimReply>(A, "release_claim", &params));
        assert!(early.starts_with("cooldown:"), "{early}");
        hub.advance(WAIT_MS).unwrap();
        assert!(hub.call_typed::<_, ReleaseClaimReply>(A, "release_claim", &params).is_ok());
    }

    #[test]
    fn claims_survive_a_restart() {
        let (mut hub, platform) = setup();
        claim_and_record(&mut hub, &platform, A, A_ID, 5, 1);
        hub.restart().unwrap();
        assert_eq!(list(&mut hub, A, ListClaimsParams::default()).unwrap().Claims[0].GridId, 5);
    }
}
