// Your server code for the Server Object type construct_world. The generator writes this file once and never again.
use crate::*;
use ckx_sdk::prelude::*;

impl Functions for ConstructWorldState {
    fn list_programs(&mut self, ctx: &Ctx, call: &Call<'_>) -> Result<ListProgramsReply> {
        Err(Error::new("list_programs is not written yet"))
    }

    fn pulse(&mut self, ctx: &Ctx) -> Result<()> {
        Ok(())
    }
}
