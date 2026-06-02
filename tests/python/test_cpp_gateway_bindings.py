import gc

import pytest


cpp = pytest.importorskip("_backtester_cpp")


def _book_update(instrument_id=10, timestamp_ns=1, price=101_250_000_000, size=5):
    return cpp.BookUpdate(
        instrument_id=instrument_id,
        timestamp_ns=timestamp_ns,
        seq_no=0,
        side=cpp.Side.Bid,
        price=price,
        size=size,
    )


def _new_gateway(trading_engine_id=1, batch_size=1):
    channel = cpp.OrderChannel(
        request_batch_size=batch_size, event_batch_size=batch_size
    )
    client = cpp.OrderGatewayClient(
        trading_engine_id=trading_engine_id, channel=channel
    )
    server = cpp.OrderGatewayServer()
    server.register_engine(trading_engine_id, channel)
    return channel, client, server


def test_python_can_create_market_data_publisher():
    publisher = cpp.MarketDataPublisher()

    assert publisher.subscriber_count() == 0


def test_python_can_subscribe_to_instrument_with_callback():
    publisher = cpp.MarketDataPublisher()
    received = []

    subscription = publisher.subscribe(
        10, lambda message: received.append(message), batch_size=1
    )

    assert subscription.subscriber_id == 1
    assert subscription.instrument_id == 10
    assert publisher.subscriber_count() == 1
    assert publisher.subscriber_count(10) == 1
    assert received == []


def test_publish_update_flush_and_subscriber_drain_invokes_callback_once():
    publisher = cpp.MarketDataPublisher()
    received = []
    subscription = publisher.subscribe(
        10, lambda message: received.append(message), batch_size=8
    )

    assigned_seq_no = publisher.publish_update(_book_update(instrument_id=10))
    publisher.flush()

    assert assigned_seq_no == 1
    assert subscription.drain_available() == 1
    assert len(received) == 1
    assert isinstance(received[0], cpp.BookUpdate)
    assert received[0].instrument_id == 10
    assert received[0].seq_no == 1


def test_subscriber_for_one_instrument_does_not_receive_other_instrument_update():
    publisher = cpp.MarketDataPublisher()
    received = []
    subscription = publisher.subscribe(
        1, lambda message: received.append(message), batch_size=1
    )

    publisher.publish_update(_book_update(instrument_id=2))
    publisher.flush()

    assert subscription.drain_available() == 0
    assert received == []


def test_python_can_create_order_channel_client_and_server():
    channel, client, server = _new_gateway(trading_engine_id=7)

    assert channel is not None
    assert client is not None
    assert server.engine_count() == 1


def test_client_send_order_server_drain_and_client_drain_produces_ack_callback():
    _, client, server = _new_gateway()
    acks = []
    client.on_ack(lambda ack: acks.append(ack))

    order_id = client.send_order(
        instrument_id=10,
        side=cpp.Side.Bid,
        price=101_250_000_000,
        size=5,
        timestamp_ns=1,
    )

    assert server.drain_requests() == 1
    server.flush_events()
    assert client.drain_events() == 1
    assert len(acks) == 1
    assert acks[0].order_id == order_id
    assert acks[0].ack_type == cpp.OrderAckType.NewAccepted
    assert acks[0].status == cpp.OrderStatus.Accepted


def test_server_emit_fill_produces_fill_callback():
    _, client, server = _new_gateway()
    fills = []
    client.on_fill(lambda fill: fills.append(fill))

    order_id = client.send_order(10, cpp.Side.Bid, 101_250_000_000, 5, 1)
    assert server.drain_requests() == 1
    server.flush_events()
    assert client.drain_events() == 1

    assert server.emit_fill(1, order_id, 101_300_000_000, 2, 2) is True
    server.flush_events()

    assert client.drain_events() == 1
    assert len(fills) == 1
    assert fills[0].order_id == order_id
    assert fills[0].fill_price == 101_300_000_000
    assert fills[0].fill_size == 2
    assert fills[0].remaining_size == 3
    assert fills[0].status == cpp.OrderStatus.PartiallyFilled


def test_invalid_order_produces_reject_callback():
    _, client, server = _new_gateway()
    rejects = []
    client.on_reject(lambda reject: rejects.append(reject))

    order_id = client.send_order(
        instrument_id=0,
        side=cpp.Side.Bid,
        price=101_250_000_000,
        size=5,
        timestamp_ns=1,
    )

    assert server.drain_requests() == 1
    server.flush_events()
    assert client.drain_events() == 1
    assert len(rejects) == 1
    assert rejects[0].order_id == order_id
    assert rejects[0].reason == cpp.OrderRejectReason.InvalidInstrument
    assert rejects[0].status == cpp.OrderStatus.Rejected


def test_multiple_clients_with_different_engine_ids_are_routed_independently():
    channel1 = cpp.OrderChannel(request_batch_size=1, event_batch_size=1)
    channel2 = cpp.OrderChannel(request_batch_size=1, event_batch_size=1)
    client1 = cpp.OrderGatewayClient(trading_engine_id=1, channel=channel1)
    client2 = cpp.OrderGatewayClient(trading_engine_id=2, channel=channel2)
    server = cpp.OrderGatewayServer()
    server.register_engine(1, channel1)
    server.register_engine(2, channel2)
    acks1 = []
    acks2 = []
    client1.on_ack(lambda ack: acks1.append(ack))
    client2.on_ack(lambda ack: acks2.append(ack))

    order_id1 = client1.send_order(10, cpp.Side.Bid, 101_250_000_000, 5, 1)
    order_id2 = client2.send_order(10, cpp.Side.Ask, 101_260_000_000, 7, 1)

    assert order_id1 == order_id2 == 1
    assert server.drain_requests() == 2
    server.flush_events()
    assert client1.drain_events() == 1
    assert client2.drain_events() == 1
    assert [ack.trading_engine_id for ack in acks1] == [1]
    assert [ack.trading_engine_id for ack in acks2] == [2]
    assert acks1[0].side == cpp.Side.Bid
    assert acks2[0].side == cpp.Side.Ask


def test_no_callback_is_invoked_after_object_destruction_smoke():
    received = []

    def create_and_destroy_objects():
        publisher = cpp.MarketDataPublisher()
        publisher.subscribe(10, lambda message: received.append(message), batch_size=1)

        channel = cpp.OrderChannel(request_batch_size=1, event_batch_size=1)
        client = cpp.OrderGatewayClient(trading_engine_id=1, channel=channel)
        client.on_ack(lambda ack: received.append(ack))

    create_and_destroy_objects()
    gc.collect()

    assert received == []
