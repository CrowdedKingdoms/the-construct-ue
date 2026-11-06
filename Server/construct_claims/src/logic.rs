// Your server code for the Server Object type construct_claims. The generator writes this file once and never again.
use crate::*;
use ckx_sdk::prelude::*;

impl Functions for ConstructClaimsState {
    fn list_claims(&mut self, ctx: &Ctx, call: &Call<'_>, params: ListClaimsParams) -> Result<ListClaimsReply> {
        Err(Error::new("list_claims is not written yet"))
    }

    fn record_claim(&mut self, ctx: &Ctx, call: &Call<'_>, params: RecordClaimParams) -> Result<RecordClaimReply> {
        Err(Error::new("record_claim is not written yet"))
    }

    fn release_claim(&mut self, ctx: &Ctx, call: &Call<'_>, params: ReleaseClaimParams) -> Result<ReleaseClaimReply> {
        Err(Error::new("release_claim is not written yet"))
    }

    fn forget_claim(&mut self, ctx: &Ctx, call: &Call<'_>, params: ForgetClaimParams) -> Result<ForgetClaimReply> {
        Err(Error::new("forget_claim is not written yet"))
    }
}
