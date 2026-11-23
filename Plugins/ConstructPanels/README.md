# Construct Panels

Native runtime webcam controls and overhead player video cards, using the shared Construct shell style.

Enable Webcam in Crowdy Construct project settings first; it is off by default. Each player then opts in separately.
C toggles Camera; F6 toggles Show Video; F4 releases the pointer for buttons. Settings and Report open centred in the
player's viewport. Closing them returns input through the shell's existing modal owner.

Camera capture and nearby feeds come from ConstructWebcam. Video settings, parental consent, Block and Report use
the SDK's Safety calls. Block and Report stay unavailable until the platform confirms the pawn's registered owner.
A video sender can forge another actor's label; confirming its registered owner does not authenticate frame origin.

No sample-video source or loose JPEG fixtures remain. The UI saves no frames or parental email.
