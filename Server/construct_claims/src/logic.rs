// Your server code for the Server Object type construct_claims. The generator writes this file once and never again.
use crate::*;
use ckx_sdk::prelude::*;

const CLAIMS_PER_PLAYER: usize = 64;
const CLAIMS_IN_ALL: usize = 10_000;
const LIST_DEFAULT: usize = 200;

// The caller's user id as the int64 a claim stores; a developer or another Server Object is not a player.
fn calling_player(call: &Call<'_>) -> Option<i64> {
    call.player().ok().and_then(|player| i64::try_from(player).ok())
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
        let owner = calling_player(call).ok_or_else(|| Error::new("a player must call this"))?;
        if params.GridId < 1 {
            return Err(Error::new("the grid id must be positive"));
        }
        if let Some(claim) = self.Claims.get(&params.GridId) {
            if claim.OwnerUserId != owner {
                return Err(Error::new("this grid is recorded for another player"));
            }
            return Ok(RecordClaimReply { bRecorded: false, Claim: claim.clone() });
        }
        if self.Claims.values().any(|claim| claim.Chunk == params.Chunk && claim.OwnerUserId != owner) {
            return Err(Error::new("this chunk is recorded for another player's grid"));
        }
        // A refusal below rolls this back: a failed call leaves the state as it was.
        self.Claims.retain(|_, claim| claim.Chunk != params.Chunk);
        if self.Claims.values().filter(|claim| claim.OwnerUserId == owner).count() >= CLAIMS_PER_PLAYER {
            return Err(Error::new("a player keeps at most 64 claims"));
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
    use ckx_sdk::Status;

    const A: Caller = Caller::Player(101);
    const B: Caller = Caller::Player(202);
    const DEVELOPER: Caller = Caller::Developer(9);
    const WAIT_MS: u64 = 1_000;

    fn hub() -> TestHub<ServerObject> {
        TestHub::spawn("construct_claims", "main").unwrap()
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

    #[test]
    fn a_record_stores_the_caller_as_owner_and_saves() {
        let mut hub = hub();
        let reply = record(&mut hub, A, 5, 1).unwrap();
        assert!(reply.bRecorded);
        assert_eq!(reply.Claim.GridId, 5);
        assert_eq!(reply.Claim.Chunk, chunk(1));
        assert_eq!(reply.Claim.OwnerUserId, 101);
        assert!(reply.Claim.ClaimedAt > 0);
        assert_eq!(hub.persist_asked(), 1);
    }

    #[test]
    fn a_grid_id_below_one_is_refused_and_one_is_not() {
        let mut hub = hub();
        assert_eq!(refusal(record(&mut hub, A, 0, 1)), "the grid id must be positive");
        assert_eq!(refusal(record(&mut hub, A, -3, 1)), "the grid id must be positive");
        assert!(record(&mut hub, A, 1, 1).unwrap().bRecorded);
    }

    #[test]
    fn another_players_grid_is_refused_and_the_callers_own_answers_unchanged() {
        let mut hub = hub();
        let first = record(&mut hub, A, 5, 1).unwrap();
        assert_eq!(refusal(record(&mut hub, B, 5, 2)), "this grid is recorded for another player");
        let again = record(&mut hub, A, 5, 9).unwrap();
        assert!(!again.bRecorded);
        assert_eq!(again.Claim, first.Claim);
    }

    #[test]
    fn a_chunk_held_by_another_players_grid_is_refused() {
        let mut hub = hub();
        record(&mut hub, A, 5, 1).unwrap();
        assert_eq!(refusal(record(&mut hub, B, 6, 1)), "this chunk is recorded for another player's grid");
        assert!(record(&mut hub, B, 6, 2).unwrap().bRecorded);
    }

    #[test]
    fn a_new_grid_replaces_the_callers_own_on_that_chunk() {
        let mut hub = hub();
        record(&mut hub, A, 5, 1).unwrap();
        assert!(record(&mut hub, A, 6, 1).unwrap().bRecorded);
        let all = list(&mut hub, A, ListClaimsParams::default()).unwrap();
        assert_eq!(all.Total, 1);
        assert_eq!(all.Claims[0].GridId, 6);
    }

    #[test]
    fn a_65th_claim_is_refused_until_one_is_released() {
        let mut hub = hub();
        for grid in 1..=64 {
            record(&mut hub, A, grid, grid).unwrap();
        }
        assert_eq!(refusal(record(&mut hub, A, 65, 65)), "a player keeps at most 64 claims");
        assert!(record(&mut hub, B, 66, 66).unwrap().bRecorded);
        release(&mut hub, A, 1).unwrap();
        assert!(record(&mut hub, A, 65, 65).unwrap().bRecorded);
    }

    #[test]
    fn replacing_a_claim_at_the_cap_is_allowed() {
        let mut hub = hub();
        for grid in 1..=64 {
            record(&mut hub, A, grid, grid).unwrap();
        }
        assert!(record(&mut hub, A, 100, 1).unwrap().bRecorded);
        assert_eq!(list(&mut hub, A, ListClaimsParams::default()).unwrap().Total, 64);
    }

    #[test]
    fn a_full_registry_is_refused_until_a_claim_leaves() {
        let mut hub = hub();
        for grid in 1..=CLAIMS_IN_ALL as i64 {
            let claim = ConstructHubClaim { GridId: grid, Chunk: chunk(grid), OwnerUserId: grid + 1000, ClaimedAt: 1 };
            hub.hub_mut().state.Claims.insert(grid, claim);
        }
        assert_eq!(refusal(record(&mut hub, A, 20_000, 20_000)), "the claim registry is full");
        hub.hub_mut().state.Claims.remove(&1);
        assert!(record(&mut hub, A, 20_000, 20_000).unwrap().bRecorded);
    }

    #[test]
    fn a_refusal_after_the_drop_keeps_the_callers_claim() {
        let mut hub = hub();
        for grid in 1..=CLAIMS_IN_ALL as i64 {
            let claim = ConstructHubClaim { GridId: grid, Chunk: chunk(grid), OwnerUserId: 101 + (grid % 2) * 1000, ClaimedAt: 1 };
            hub.hub_mut().state.Claims.insert(grid, claim);
        }
        assert_eq!(refusal(record(&mut hub, A, 20_000, 2)), "a player keeps at most 64 claims");
        assert!(hub.hub_mut().state.Claims.contains_key(&2));
    }

    #[test]
    fn a_developer_cannot_record_or_release() {
        let mut hub = hub();
        record(&mut hub, A, 5, 1).unwrap();
        assert_eq!(refusal(hub.call_typed::<_, RecordClaimReply>(DEVELOPER, "record_claim", &record_params(6, 2))), "a player must call this");
        assert_eq!(refusal(hub.call_typed::<_, ReleaseClaimReply>(DEVELOPER, "release_claim", &ReleaseClaimParams { GridId: 5 })), "a player must call this");
        assert!(release(&mut hub, A, 5).unwrap().bReleased);
    }

    #[test]
    fn only_the_owner_releases_and_an_unknown_grid_answers_false() {
        let mut hub = hub();
        record(&mut hub, A, 5, 1).unwrap();
        assert_eq!(refusal(release(&mut hub, B, 5)), "only the claim's owner may release it");
        assert!(!release(&mut hub, A, 99).unwrap().bReleased);
        assert!(release(&mut hub, A, 5).unwrap().bReleased);
        assert_eq!(hub.persist_asked(), 2);
    }

    #[test]
    fn only_a_developer_forgets_and_it_logs_no_id() {
        let mut hub = hub();
        record(&mut hub, A, 5_123, 1).unwrap();
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
        let mut hub = hub();
        record(&mut hub, A, 30, 3).unwrap();
        record(&mut hub, B, 10, 1).unwrap();
        record(&mut hub, A, 20, 2).unwrap();

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
        let mut hub = hub();
        for grid in 1..=250 {
            let claim = ConstructHubClaim { GridId: grid, Chunk: chunk(grid), OwnerUserId: 101, ClaimedAt: 1 };
            hub.hub_mut().state.Claims.insert(grid, claim);
        }
        assert_eq!(list(&mut hub, A, ListClaimsParams::default()).unwrap().Claims.len(), 200);
        assert_eq!(list(&mut hub, A, ListClaimsParams { Limit: 1000, ..Default::default() }).unwrap().Claims.len(), 250);
        assert!(refusal(list(&mut hub, A, ListClaimsParams { Limit: 1001, ..Default::default() })).starts_with("bad_params:"));
    }

    #[test]
    fn only_mine_from_a_developer_is_refused_and_a_plain_list_is_not() {
        let mut hub = hub();
        let refused = hub.call_typed::<_, ListClaimsReply>(DEVELOPER, "list_claims", &ListClaimsParams { bOnlyMine: true, ..Default::default() });
        assert_eq!(refusal(refused), "only a player has claims of their own");
        assert_eq!(hub.call_typed::<_, ListClaimsReply>(DEVELOPER, "list_claims", &ListClaimsParams::default()).unwrap().Total, 0);
    }

    #[test]
    fn the_cooldown_refuses_inside_a_second_and_a_refusal_does_not_start_it() {
        let mut hub = hub();
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
        let mut hub = hub();
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
        let mut hub = hub();
        let params = ReleaseClaimParams { GridId: 99 };
        assert!(hub.call_typed::<_, ReleaseClaimReply>(A, "release_claim", &params).is_ok());
        let early = refusal(hub.call_typed::<_, ReleaseClaimReply>(A, "release_claim", &params));
        assert!(early.starts_with("cooldown:"), "{early}");
        hub.advance(WAIT_MS).unwrap();
        assert!(hub.call_typed::<_, ReleaseClaimReply>(A, "release_claim", &params).is_ok());
    }

    #[test]
    fn claims_survive_a_restart() {
        let mut hub = hub();
        record(&mut hub, A, 5, 1).unwrap();
        hub.restart().unwrap();
        assert_eq!(list(&mut hub, A, ListClaimsParams::default()).unwrap().Claims[0].GridId, 5);
    }
}
