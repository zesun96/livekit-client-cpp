mod support;
use livekit_client_cpp::*;
use support::block_on;

#[test]
fn async_room_thread_affinity_cancellation_and_shutdown() {
    fn send_sync<T: Send + Sync>() {}
    fn send<T: Send>() {}
    send_sync::<AsyncRoomHandle>();
    send::<Operation<Vec<Participant>>>();
    let sdk = Sdk::new().unwrap();
    assert!(sdk.async_room_with_capacity(0, 1).is_err());
    let room = sdk.async_room().unwrap();
    let handle = room.handle();
    drop(sdk);
    assert!(!block_on(handle.is_connected()).unwrap());
    assert_eq!(
        block_on(handle.state()).unwrap(),
        ffi::LK_ROOM_STATE_DISCONNECTED
    );
    assert!(block_on(handle.remote_participants()).unwrap().is_empty());
    assert!(block_on(handle.publish_data(&[0, 255], "offline", true)).is_err());
    assert!(block_on(handle.connect("invalid\0url", "token")).is_err());
    let ignored = handle.set_name("queued name");
    drop(ignored);
    // A later command is a barrier: cancellation did not free the worker or room.
    block_on(handle.state()).unwrap();
    block_on(handle.register_rpc_method("echo", |i| RpcResponse::Success(i.payload))).unwrap();
    block_on(handle.unregister_rpc_method("echo")).unwrap();
    let second = handle.clone();
    std::thread::spawn(move || assert!(block_on(second.remote_tracks()).unwrap().is_empty()))
        .join()
        .unwrap();
    drop(room); // A surviving command handle must not keep the owner join waiting.
    assert!(block_on(handle.state()).is_err());
}
