use livekit_client_cpp::{ffi, Sdk};

#[test]
fn offline_lifecycle_snapshots_and_failures() {
    let sdk = Sdk::new().unwrap();
    let second = Sdk::new().unwrap();
    assert!(!sdk.version().unwrap().is_empty());
    assert!(sdk.room_with_event_capacity(0).is_err());
    let mut room = sdk.room().unwrap();
    drop(sdk);
    drop(second);
    // The room still owns the initialized SDK.
    assert!(!room.is_connected());
    assert_eq!(room.state(), ffi::LK_ROOM_STATE_DISCONNECTED);
    assert!(room.remote_participants().unwrap().is_empty());
    let local = room.local_participant().unwrap();
    let failure = room
        .publish_data(&[0, 255, 0], "offline", true)
        .unwrap_err();
    assert_ne!(failure.status, ffi::LK_STATUS_OK);
    assert!(!failure.message.is_empty());
    let saved = failure.message.clone();
    assert!(room.send_text("offline", "test").is_err());
    assert!(room.send_bytes(&[], "test").is_err());
    assert_eq!(failure.message, saved);
    assert_eq!(
        room.connect("invalid\0url", "token").unwrap_err().status,
        ffi::LK_STATUS_INVALID_ARGUMENT
    );
    drop(room);
    // Participant values are copied out of the destroyed snapshot and room.
    assert!(local.identity.is_empty());
    let restarted = Sdk::new().unwrap();
    assert!(!restarted.version().unwrap().is_empty());
    drop(restarted.room().unwrap());
}
