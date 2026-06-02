#include "domain/Types.hpp"
#include "gateway/OrderChannel.hpp"
#include "gateway/OrderGatewayClient.hpp"
#include "gateway/OrderGatewayServer.hpp"
#include "gateway/OrderMessage.hpp"
#include "messaging/MarketDataMessage.hpp"
#include "messaging/MarketDataPublisher.hpp"
#include "messaging/MarketDataSubscriber.hpp"

#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include <memory>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#ifndef BACKTESTER_VERSION
#define BACKTESTER_VERSION "0.0.0"
#endif

namespace py = pybind11;

namespace
{

template <typename Message>
void bindOrderFields(py::class_<Message>& cls)
{
    cls.def_property(
           "trading_engine_id",
           [](const Message& message)
           { return message.fields.trading_engine_id; },
           [](Message& message, md::TradingEngineId value)
           { message.fields.trading_engine_id = value; })
        .def_property(
            "order_id",
            [](const Message& message)
            { return message.fields.order_id; },
            [](Message& message, md::OrderId value)
            { message.fields.order_id = value; })
        .def_property(
            "instrument_id",
            [](const Message& message)
            { return message.fields.instrument_id; },
            [](Message& message, md::InstrumentId value)
            { message.fields.instrument_id = value; })
        .def_property(
            "side",
            [](const Message& message)
            { return message.fields.side; },
            [](Message& message, md::Side value)
            { message.fields.side = value; })
        .def_property(
            "price",
            [](const Message& message)
            { return message.fields.price; },
            [](Message& message, md::Price value)
            { message.fields.price = value; })
        .def_property(
            "size",
            [](const Message& message)
            { return message.fields.size; },
            [](Message& message, md::Quantity value)
            { message.fields.size = value; })
        .def_property(
            "timestamp_ns",
            [](const Message& message)
            { return message.fields.timestamp_ns; },
            [](Message& message, md::TimestampNs value)
            { message.fields.timestamp_ns = value; })
        .def_property(
            "status",
            [](const Message& message)
            { return message.fields.status; },
            [](Message& message, md::OrderStatus value)
            { message.fields.status = value; });
}

md::OrderFields makeOrderFields(
    md::TradingEngineId trading_engine_id,
    md::OrderId order_id,
    md::InstrumentId instrument_id,
    md::Side side,
    md::Price price,
    md::Quantity size,
    md::TimestampNs timestamp_ns,
    md::OrderStatus status)
{
    return md::OrderFields{
        .trading_engine_id = trading_engine_id,
        .order_id = order_id,
        .instrument_id = instrument_id,
        .side = side,
        .price = price,
        .size = size,
        .timestamp_ns = timestamp_ns,
        .status = status,
    };
}

void invokeMarketDataCallback(const py::function& callback, const md::MarketDataMessage& message)
{
    py::gil_scoped_acquire gil;
    std::visit(
        [&callback](const auto& payload)
        {
            using Payload = std::decay_t<decltype(payload)>;
            callback(Payload{payload});
        },
        message);
}

} // namespace

PYBIND11_MODULE(_backtester_cpp, module)
{
    module.doc() = "C++ extension module for the backtester Strategy API";

    module.def("version", []
               { return BACKTESTER_VERSION; });
    module.attr("PRICE_SCALE") = py::int_(md::price_scale);
    module.attr("UNDEFINED_PRICE") = py::int_(md::undefined_price);

    py::enum_<md::Side>(module, "Side")
        .value("Ask", md::Side::Ask)
        .value("Bid", md::Side::Bid)
        .value("None", md::Side::None)
        .value("ASK", md::Side::Ask)
        .value("BID", md::Side::Bid)
        .value("NONE", md::Side::None)
        .export_values();

    py::enum_<md::OrderStatus>(module, "OrderStatus")
        .value("New", md::OrderStatus::New)
        .value("Accepted", md::OrderStatus::Accepted)
        .value("ModifyRequested", md::OrderStatus::ModifyRequested)
        .value("CancelRequested", md::OrderStatus::CancelRequested)
        .value("PartiallyFilled", md::OrderStatus::PartiallyFilled)
        .value("Filled", md::OrderStatus::Filled)
        .value("Cancelled", md::OrderStatus::Cancelled)
        .value("Rejected", md::OrderStatus::Rejected)
        .value("NEW", md::OrderStatus::New)
        .value("ACCEPTED", md::OrderStatus::Accepted)
        .value("MODIFY_REQUESTED", md::OrderStatus::ModifyRequested)
        .value("CANCEL_REQUESTED", md::OrderStatus::CancelRequested)
        .value("PARTIALLY_FILLED", md::OrderStatus::PartiallyFilled)
        .value("FILLED", md::OrderStatus::Filled)
        .value("CANCELLED", md::OrderStatus::Cancelled)
        .value("REJECTED", md::OrderStatus::Rejected)
        .export_values();

    py::enum_<md::OrderAckType>(module, "OrderAckType")
        .value("NewAccepted", md::OrderAckType::NewAccepted)
        .value("ModifyAccepted", md::OrderAckType::ModifyAccepted)
        .value("CancelAccepted", md::OrderAckType::CancelAccepted)
        .value("NEW_ACCEPTED", md::OrderAckType::NewAccepted)
        .value("MODIFY_ACCEPTED", md::OrderAckType::ModifyAccepted)
        .value("CANCEL_ACCEPTED", md::OrderAckType::CancelAccepted)
        .export_values();

    py::enum_<md::OrderRejectReason>(module, "OrderRejectReason")
        .value("DuplicateOrderId", md::OrderRejectReason::DuplicateOrderId)
        .value("UnknownOrderId", md::OrderRejectReason::UnknownOrderId)
        .value("InvalidInstrument", md::OrderRejectReason::InvalidInstrument)
        .value("InvalidSide", md::OrderRejectReason::InvalidSide)
        .value("InvalidPrice", md::OrderRejectReason::InvalidPrice)
        .value("InvalidQuantity", md::OrderRejectReason::InvalidQuantity)
        .value("AlreadyTerminal", md::OrderRejectReason::AlreadyTerminal)
        .value("InternalError", md::OrderRejectReason::InternalError)
        .value("DUPLICATE_ORDER_ID", md::OrderRejectReason::DuplicateOrderId)
        .value("UNKNOWN_ORDER_ID", md::OrderRejectReason::UnknownOrderId)
        .value("INVALID_INSTRUMENT", md::OrderRejectReason::InvalidInstrument)
        .value("INVALID_SIDE", md::OrderRejectReason::InvalidSide)
        .value("INVALID_PRICE", md::OrderRejectReason::InvalidPrice)
        .value("INVALID_QUANTITY", md::OrderRejectReason::InvalidQuantity)
        .value("ALREADY_TERMINAL", md::OrderRejectReason::AlreadyTerminal)
        .value("INTERNAL_ERROR", md::OrderRejectReason::InternalError)
        .export_values();

    py::class_<md::PriceLevel>(module, "PriceLevel")
        .def(
            py::init<std::uint32_t, md::Price, md::Quantity>(),
            py::arg("level_index") = std::uint32_t{},
            py::arg("price") = md::Price{},
            py::arg("size") = md::Quantity{})
        .def_readwrite("level_index", &md::PriceLevel::level_index)
        .def_readwrite("price", &md::PriceLevel::price)
        .def_readwrite("size", &md::PriceLevel::size);

    py::class_<md::BookUpdate>(module, "BookUpdate")
        .def(
            py::init<md::InstrumentId, md::TimestampNs, md::SeqNo, md::Side, md::Price, md::Quantity>(),
            py::arg("instrument_id") = md::InstrumentId{},
            py::arg("timestamp_ns") = md::TimestampNs{},
            py::arg("seq_no") = md::SeqNo{},
            py::arg("side") = md::Side::None,
            py::arg("price") = md::Price{},
            py::arg("size") = md::Quantity{})
        .def_readwrite("instrument_id", &md::BookUpdate::instrument_id)
        .def_readwrite("timestamp_ns", &md::BookUpdate::timestamp_ns)
        .def_readwrite("seq_no", &md::BookUpdate::seq_no)
        .def_readwrite("side", &md::BookUpdate::side)
        .def_readwrite("price", &md::BookUpdate::price)
        .def_readwrite("size", &md::BookUpdate::size);

    py::class_<md::BookSnapshot>(module, "BookSnapshot")
        .def(
            py::init<
                md::InstrumentId,
                md::TimestampNs,
                md::SeqNo,
                std::vector<md::PriceLevel>,
                std::vector<md::PriceLevel>>(),
            py::arg("instrument_id") = md::InstrumentId{},
            py::arg("timestamp_ns") = md::TimestampNs{},
            py::arg("seq_no") = md::SeqNo{},
            py::arg("bids") = std::vector<md::PriceLevel>{},
            py::arg("asks") = std::vector<md::PriceLevel>{})
        .def_readwrite("instrument_id", &md::BookSnapshot::instrument_id)
        .def_readwrite("timestamp_ns", &md::BookSnapshot::timestamp_ns)
        .def_readwrite("seq_no", &md::BookSnapshot::seq_no)
        .def_readwrite("bids", &md::BookSnapshot::bids)
        .def_readwrite("asks", &md::BookSnapshot::asks);

    py::class_<md::Trade>(module, "Trade")
        .def(
            py::init<md::InstrumentId, md::TimestampNs, md::SeqNo, md::Price, md::Quantity, md::Side>(),
            py::arg("instrument_id") = md::InstrumentId{},
            py::arg("timestamp_ns") = md::TimestampNs{},
            py::arg("seq_no") = md::SeqNo{},
            py::arg("price") = md::Price{},
            py::arg("size") = md::Quantity{},
            py::arg("aggressor_side") = md::Side::None)
        .def_readwrite("instrument_id", &md::Trade::instrument_id)
        .def_readwrite("timestamp_ns", &md::Trade::timestamp_ns)
        .def_readwrite("seq_no", &md::Trade::seq_no)
        .def_readwrite("price", &md::Trade::price)
        .def_readwrite("size", &md::Trade::size)
        .def_readwrite("aggressor_side", &md::Trade::aggressor_side);

    auto order_ack = py::class_<md::OrderAck>(module, "OrderAck")
                         .def(
                             py::init(
                                 [](md::TradingEngineId trading_engine_id,
                                    md::OrderId order_id,
                                    md::InstrumentId instrument_id,
                                    md::Side side,
                                    md::Price price,
                                    md::Quantity size,
                                    md::TimestampNs timestamp_ns,
                                    md::OrderStatus status,
                                    md::OrderAckType ack_type)
                                 {
                                     return md::OrderAck{
                                         .fields = makeOrderFields(
                                             trading_engine_id,
                                             order_id,
                                             instrument_id,
                                             side,
                                             price,
                                             size,
                                             timestamp_ns,
                                             status),
                                         .ack_type = ack_type,
                                     };
                                 }),
                             py::arg("trading_engine_id") = md::TradingEngineId{},
                             py::arg("order_id") = md::OrderId{},
                             py::arg("instrument_id") = md::InstrumentId{},
                             py::arg("side") = md::Side::None,
                             py::arg("price") = md::Price{},
                             py::arg("size") = md::Quantity{},
                             py::arg("timestamp_ns") = md::TimestampNs{},
                             py::arg("status") = md::OrderStatus::New,
                             py::arg("ack_type") = md::OrderAckType::NewAccepted)
                         .def_readwrite("ack_type", &md::OrderAck::ack_type);
    bindOrderFields(order_ack);

    auto order_fill = py::class_<md::OrderFill>(module, "OrderFill")
                          .def(
                              py::init(
                                  [](md::TradingEngineId trading_engine_id,
                                     md::OrderId order_id,
                                     md::InstrumentId instrument_id,
                                     md::Side side,
                                     md::Price price,
                                     md::Quantity size,
                                     md::TimestampNs timestamp_ns,
                                     md::OrderStatus status,
                                     md::Price fill_price,
                                     md::Quantity fill_size,
                                     md::Quantity remaining_size)
                                  {
                                      return md::OrderFill{
                                          .fields = makeOrderFields(
                                              trading_engine_id,
                                              order_id,
                                              instrument_id,
                                              side,
                                              price,
                                              size,
                                              timestamp_ns,
                                              status),
                                          .fill_price = fill_price,
                                          .fill_size = fill_size,
                                          .remaining_size = remaining_size,
                                      };
                                  }),
                              py::arg("trading_engine_id") = md::TradingEngineId{},
                              py::arg("order_id") = md::OrderId{},
                              py::arg("instrument_id") = md::InstrumentId{},
                              py::arg("side") = md::Side::None,
                              py::arg("price") = md::Price{},
                              py::arg("size") = md::Quantity{},
                              py::arg("timestamp_ns") = md::TimestampNs{},
                              py::arg("status") = md::OrderStatus::New,
                              py::arg("fill_price") = md::Price{},
                              py::arg("fill_size") = md::Quantity{},
                              py::arg("remaining_size") = md::Quantity{})
                          .def_readwrite("fill_price", &md::OrderFill::fill_price)
                          .def_readwrite("fill_size", &md::OrderFill::fill_size)
                          .def_readwrite("remaining_size", &md::OrderFill::remaining_size);
    bindOrderFields(order_fill);

    auto order_reject = py::class_<md::OrderReject>(module, "OrderReject")
                            .def(
                                py::init(
                                    [](md::TradingEngineId trading_engine_id,
                                       md::OrderId order_id,
                                       md::InstrumentId instrument_id,
                                       md::Side side,
                                       md::Price price,
                                       md::Quantity size,
                                       md::TimestampNs timestamp_ns,
                                       md::OrderStatus status,
                                       md::OrderRejectReason reason,
                                       std::string text)
                                    {
                                        return md::OrderReject{
                                            .fields = makeOrderFields(
                                                trading_engine_id,
                                                order_id,
                                                instrument_id,
                                                side,
                                                price,
                                                size,
                                                timestamp_ns,
                                                status),
                                            .reason = reason,
                                            .text = std::move(text),
                                        };
                                    }),
                                py::arg("trading_engine_id") = md::TradingEngineId{},
                                py::arg("order_id") = md::OrderId{},
                                py::arg("instrument_id") = md::InstrumentId{},
                                py::arg("side") = md::Side::None,
                                py::arg("price") = md::Price{},
                                py::arg("size") = md::Quantity{},
                                py::arg("timestamp_ns") = md::TimestampNs{},
                                py::arg("status") = md::OrderStatus::New,
                                py::arg("reason") = md::OrderRejectReason::InternalError,
                                py::arg("text") = std::string{})
                            .def_readwrite("reason", &md::OrderReject::reason)
                            .def_readwrite("text", &md::OrderReject::text);
    bindOrderFields(order_reject);

    py::class_<md::MarketDataSubscriber, std::shared_ptr<md::MarketDataSubscriber>>(module, "MarketDataSubscriber")
        .def("drain_available", &md::MarketDataSubscriber::drainAvailable)
        .def("pending_approx", &md::MarketDataSubscriber::pendingApprox)
        .def("flush", &md::MarketDataSubscriber::flush);

    py::class_<md::MarketDataSubscription>(module, "MarketDataSubscription")
        .def_readonly("subscriber_id", &md::MarketDataSubscription::subscriber_id)
        .def_readonly("instrument_id", &md::MarketDataSubscription::instrument_id)
        .def_readonly("subscriber", &md::MarketDataSubscription::subscriber)
        .def(
            "drain_available",
            [](md::MarketDataSubscription& subscription)
            {
                return subscription.subscriber->drainAvailable();
            })
        .def(
            "pending_approx",
            [](const md::MarketDataSubscription& subscription)
            {
                return subscription.subscriber->pendingApprox();
            });

    py::class_<md::MarketDataPublisher>(module, "MarketDataPublisher")
        .def(py::init<>())
        .def(
            "subscribe",
            [](md::MarketDataPublisher& publisher, md::InstrumentId instrument_id, py::function callback, std::size_t batch_size)
            {
                auto cpp_callback = [callback = std::move(callback)](const md::MarketDataMessage& message)
                {
                    invokeMarketDataCallback(callback, message);
                };
                return publisher.subscribe(instrument_id, std::move(cpp_callback), batch_size);
            },
            py::arg("instrument_id"),
            py::arg("callback"),
            py::arg("batch_size") = std::size_t{256})
        .def(
            "publish_update",
            [](md::MarketDataPublisher& publisher, md::BookUpdate update)
            {
                return publisher.publishUpdate(std::move(update));
            },
            py::arg("update"))
        .def(
            "publish_update",
            [](md::MarketDataPublisher& publisher,
               md::InstrumentId instrument_id,
               md::TimestampNs timestamp_ns,
               md::Side side,
               md::Price price,
               md::Quantity size)
            {
                return publisher.publishUpdate(md::BookUpdate{
                    .instrument_id = instrument_id,
                    .timestamp_ns = timestamp_ns,
                    .seq_no = md::SeqNo{},
                    .side = side,
                    .price = price,
                    .size = size,
                });
            },
            py::arg("instrument_id"),
            py::arg("timestamp_ns"),
            py::arg("side"),
            py::arg("price"),
            py::arg("size"))
        .def(
            "publish_snapshot",
            [](md::MarketDataPublisher& publisher, md::BookSnapshot snapshot)
            {
                return publisher.publishSnapshot(std::move(snapshot));
            },
            py::arg("snapshot"))
        .def(
            "publish_trade",
            [](md::MarketDataPublisher& publisher, md::Trade trade)
            {
                return publisher.publishTrade(std::move(trade));
            },
            py::arg("trade"))
        .def("flush", &md::MarketDataPublisher::flush)
        .def("subscriber_count", py::overload_cast<>(&md::MarketDataPublisher::subscriberCount, py::const_))
        .def(
            "subscriber_count",
            py::overload_cast<md::InstrumentId>(&md::MarketDataPublisher::subscriberCount, py::const_),
            py::arg("instrument_id"));

    py::class_<md::OrderChannel, std::shared_ptr<md::OrderChannel>>(module, "OrderChannel")
        .def(
            py::init<std::size_t, std::size_t>(),
            py::arg("request_batch_size") = std::size_t{256},
            py::arg("event_batch_size") = std::size_t{256})
        .def("flush_requests", &md::OrderChannel::flushRequests)
        .def("flush_events", &md::OrderChannel::flushEvents);

    py::class_<md::OrderGatewayClient>(module, "OrderGatewayClient")
        .def(
            py::init<md::TradingEngineId, std::shared_ptr<md::OrderChannel>>(),
            py::arg("trading_engine_id"),
            py::arg("channel"))
        .def(
            "send_order",
            [](md::OrderGatewayClient& client,
               md::InstrumentId instrument_id,
               md::Side side,
               md::Price price,
               md::Quantity size,
               md::TimestampNs timestamp_ns)
            {
                const md::OrderId order_id = client.sendOrder(instrument_id, side, price, size, timestamp_ns);
                client.flush();
                return order_id;
            },
            py::arg("instrument_id"),
            py::arg("side"),
            py::arg("price"),
            py::arg("size"),
            py::arg("timestamp_ns"))
        .def(
            "cancel_order",
            [](md::OrderGatewayClient& client, md::OrderId order_id, md::InstrumentId instrument_id, md::TimestampNs timestamp_ns)
            {
                client.cancelOrder(order_id, instrument_id, timestamp_ns);
                client.flush();
            },
            py::arg("order_id"),
            py::arg("instrument_id"),
            py::arg("timestamp_ns"))
        .def(
            "modify_order",
            [](md::OrderGatewayClient& client,
               md::OrderId order_id,
               md::InstrumentId instrument_id,
               md::Side side,
               md::Price price,
               md::Quantity size,
               md::TimestampNs timestamp_ns)
            {
                client.modifyOrder(order_id, instrument_id, side, price, size, timestamp_ns);
                client.flush();
            },
            py::arg("order_id"),
            py::arg("instrument_id"),
            py::arg("side"),
            py::arg("price"),
            py::arg("size"),
            py::arg("timestamp_ns"))
        .def("flush", &md::OrderGatewayClient::flush)
        .def("drain_events", &md::OrderGatewayClient::drainEvents)
        .def(
            "on_ack",
            [](md::OrderGatewayClient& client, py::function callback)
            {
                client.onAck(
                    [callback = std::move(callback)](const md::OrderAck& ack)
                    {
                        py::gil_scoped_acquire gil;
                        callback(md::OrderAck{ack});
                    });
            },
            py::arg("callback"))
        .def(
            "on_fill",
            [](md::OrderGatewayClient& client, py::function callback)
            {
                client.onFill(
                    [callback = std::move(callback)](const md::OrderFill& fill)
                    {
                        py::gil_scoped_acquire gil;
                        callback(md::OrderFill{fill});
                    });
            },
            py::arg("callback"))
        .def(
            "on_reject",
            [](md::OrderGatewayClient& client, py::function callback)
            {
                client.onReject(
                    [callback = std::move(callback)](const md::OrderReject& reject)
                    {
                        py::gil_scoped_acquire gil;
                        callback(md::OrderReject{reject});
                    });
            },
            py::arg("callback"));

    py::class_<md::OrderGatewayServer>(module, "OrderGatewayServer")
        .def(py::init<>())
        .def("register_engine", &md::OrderGatewayServer::registerEngine, py::arg("trading_engine_id"), py::arg("channel"))
        .def("drain_requests", &md::OrderGatewayServer::drainRequests)
        .def("flush_events", &md::OrderGatewayServer::flushEvents)
        .def(
            "emit_fill",
            &md::OrderGatewayServer::emitFill,
            py::arg("trading_engine_id"),
            py::arg("order_id"),
            py::arg("fill_price"),
            py::arg("fill_size"),
            py::arg("timestamp_ns"))
        .def("open_order_count", &md::OrderGatewayServer::openOrderCount)
        .def("engine_count", &md::OrderGatewayServer::engineCount);
}
