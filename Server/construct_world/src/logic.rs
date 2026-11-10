// Your server code for the Server Object type construct_world. The generator writes this file once and never again.
use crate::*;
use ckx_sdk::prelude::*;

impl Functions for ConstructWorldState {
    fn list_programs(&mut self, _ctx: &Ctx, _call: &Call<'_>) -> Result<ListProgramsReply> {
        // Changing the catalog is a deploy. Program 0 is the Holodeck itself and is never listed.
        Ok(ListProgramsReply {
            Programs: vec![ConstructHubProgram {
                ProgramId: 1,
                SceneId: "paint".to_string(),
                Name: "Paint".to_string(),
                Description: "A shared 3D canvas. Place and erase voxel blocks together.".to_string(),
            }],
        })
    }

    fn pulse(&mut self, ctx: &Ctx) -> Result<()> {
        self.Pulses += 1;
        self.LastPulseAt = ctx.now_ms() as i64;
        Ok(())
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use ckx_sdk::testing::TestHub;

    const PLAYER: Caller = Caller::Player(101);
    const PULSE_MS: u64 = 60_000;

    fn hub() -> TestHub<ServerObject> {
        TestHub::spawn("construct_world", "main").unwrap()
    }

    #[test]
    fn the_catalog_lists_paint_as_program_1() {
        let mut hub = hub();
        let reply: ListProgramsReply = hub.call_typed(PLAYER, "list_programs", &()).unwrap();
        assert_eq!(reply.Programs.len(), 1);
        assert_eq!(reply.Programs[0].ProgramId, 1);
        assert_eq!(reply.Programs[0].SceneId, "paint");
        assert_eq!(reply.Programs[0].Name, "Paint");
        assert_eq!(
            reply.Programs[0].Description,
            "A shared 3D canvas. Place and erase voxel blocks together."
        );
    }

    #[test]
    fn the_pulse_counts_each_minute_and_stamps_the_time() {
        let mut hub = hub();
        let start = hub.now_ms();
        hub.advance(PULSE_MS - 1).unwrap();
        assert_eq!(hub.hub_mut().state.Pulses, 0);
        hub.advance(1).unwrap();
        assert_eq!(hub.hub_mut().state.Pulses, 1);
        assert_eq!(hub.hub_mut().state.LastPulseAt, (start + PULSE_MS) as i64);
        hub.advance(2 * PULSE_MS).unwrap();
        assert_eq!(hub.hub_mut().state.Pulses, 3);
    }

    #[test]
    fn a_pulse_is_pushed_to_watchers() {
        let mut hub = hub();
        hub.advance(PULSE_MS).unwrap();
        assert!(hub.published().iter().any(|(topic, _)| topic == "state"));
    }

    #[test]
    fn the_pulse_keeps_running_after_a_restart() {
        let mut hub = hub();
        hub.advance(PULSE_MS).unwrap();
        hub.restart().unwrap();
        hub.advance(PULSE_MS).unwrap();
        assert_eq!(hub.hub_mut().state.Pulses, 2);
    }
}
