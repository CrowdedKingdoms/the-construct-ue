// Your server code for the Server Object type construct_progress. The generator writes this file once and never again.
use crate::*;
use ckx_sdk::prelude::*;

// Nothing grants XP yet, so the type has no functions: its generated glue answers the owner's watched read.
impl Functions for ConstructProgressState {
}

#[cfg(test)]
mod tests {
    use super::*;
    use ckx_sdk::testing::TestHub;
    use ckx_sdk::Status;

    const OWNER: Caller = Caller::Player(101);
    const OTHER: Caller = Caller::Player(202);

    fn hub() -> TestHub<ServerObject> {
        TestHub::spawn("construct_progress", "101").unwrap()
    }

    #[test]
    fn a_new_player_starts_at_level_1_with_nothing_earned() {
        let mut hub = hub();
        assert!(hub.call(OWNER, "read", &[]).is_ok());
        let state = &hub.hub_mut().state;
        assert_eq!((state.Xp, state.Level, state.SkillPoints), (0, 1, 0));
    }

    #[test]
    fn another_players_read_is_refused_and_the_owners_is_not() {
        let mut hub = hub();
        let error = hub.call(OTHER, "read", &[]).expect_err("the read should be refused");
        assert_eq!(error.status, Status::AppError);
        assert!(error.message.starts_with("denied:"), "{}", error.message);
        assert!(hub.call(OWNER, "read", &[]).is_ok());
    }

    #[test]
    fn an_instance_id_that_is_not_a_user_id_starts_nothing() {
        assert!(TestHub::<ServerObject>::spawn("construct_progress", "abc").is_err());
        assert!(TestHub::<ServerObject>::spawn("construct_progress", "007").is_err());
    }
}
