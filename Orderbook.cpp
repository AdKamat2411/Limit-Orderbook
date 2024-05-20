#include <format>
#include <tuple>
#include <optional>
#include <variant>
#include <memory>
#include <unordered_map>
#include <algorithm>
#include <iostream>
#include <numeric>
#include <vector>
#include <string>
#include <limits>
#include <stack>
#include <deque>
#include <ctime>
#include <cmath>
#include <list>
#include <set>
#include <map>

enum class OrderType {
    GoodTillCancel,
    FillandKill
};

enum class Side {
    Buy,
    Sell
};

using Price = std::int32_t;
using Quantity = std::uint32_t;
using OrderID = std::uint64_t;

struct LevelInfo {
    Price price_;
    Quantity quantity_; 
};

using LevelInfos = std::vector<LevelInfo>;

class OrderbookLevelInfos {
    public:
        OrderbookLevelInfos(const LevelInfos& bids, const LevelInfos& asks)
            : bids_(bids),
              asks_(asks)
        {}
        
        const LevelInfos& GetBids() const  {return bids_; }
        const LevelInfos& GetAsks() const { return asks_; }

    private:
        LevelInfos bids_;
        LevelInfos asks_;
};

class Order {
    public:
        Order(OrderType orderType, OrderID orderId, Side side, Price price, Quantity quantity)
        : orderType_(orderType),
        orderId_(orderId),
        side_(side),
        price_(price),
        intialQuantity_(quantity),
        remainingQuantity_(quantity) {}


        OrderID GetOrderID() const { return orderId_; }
        Side GetSide() const { return side_; }
        Price GetPrice() const { return price_; }
        OrderType GetOrderType() const { return orderType_; }
        Quantity GetIntialQuantity() const { return intialQuantity_; }
        Quantity GetRemainingQuanity() const { return remainingQuantity_; }
        Quantity GetFilledQuantity() const { return GetIntialQuantity() - GetRemainingQuanity(); }

        bool IsFilled() const { return GetRemainingQuanity() == 0; }

        void Fill(Quantity quantity) {
            if (quantity > GetRemainingQuanity())
                throw std::logic_error("Order (" + std::to_string(GetOrderID()) + ") cannot be filled for more than its remaining quantity.");
            
            remainingQuantity_ -= quantity;
        }
    
    private:
        OrderType orderType_;
        OrderID orderId_;
        Side side_;
        Price price_;
        Quantity intialQuantity_;
        Quantity remainingQuantity_;
};

using OrderPointer = std::shared_ptr<Order>;
using OrderPointers = std::list<OrderPointer>;

class OrderModify {
    public:
        OrderModify(OrderID orderId, Side side, Price price, Quantity quantity)
        : orderId_(orderId),
        side_(side),
        price_(price),
        quantity_(quantity) {}

        OrderID GetOrderID() const { return orderId_; }
        Side GetSide() const { return side_; }
        Price GetPrice() const { return price_; }
        Quantity GetQuantity() const { return quantity_; }

        OrderPointer ToOrderPointer(OrderType type) const {
            return std::make_shared<Order>(type, GetOrderID(), GetSide(), GetPrice(), GetQuantity());
        }
    private:
        OrderID orderId_;
        Side side_;
        Price price_;
        Quantity quantity_;
};

struct TradeInfo {
    OrderID orderId_;
    Price price_;
    Quantity quantity_;
};


class Trade {
    public:
        Trade(const TradeInfo& bidTrade, const TradeInfo& askTrade)
        : bidTrade_(bidTrade),
        askTrade_(askTrade)
        {}


    const TradeInfo& GetBidTrade() const { return bidTrade_; }
    const TradeInfo& GetAskTrade() const { return askTrade_; }

    private:
        TradeInfo bidTrade_;
        TradeInfo askTrade_;

};

using Trades = std::vector<Trade>;

class Ordebook {

    private:
        struct OrderEntry {
            OrderPointer order_;
            OrderPointers::iterator location_;
        };
        std::map<Price, OrderPointers, std::greater<Price> > bids_;
        std::map<Price, OrderPointers, std::less<Price> > asks_;
        std::unordered_map<OrderID, OrderEntry> orders_;
         
        bool CanMatch(Side side, Price price) const {
            if (side == Side::Buy) {
                 if (asks_.empty()) 
                    return false;
                const auto& [bestAsk,  _] = *asks_.begin();
                return price >= bestAsk; //Checking for order match
            } else {
                if (bids_.empty())
                    return false;
                const auto& [bestBid, _] = *bids_.begin();
                return price <= bestBid;
            }
        }

        Trades MatchOrders() {
            Trades trades;
            trades.reserve(orders_.size());

            while (true) {
                if (asks_.empty() || bids_.empty())
                    break;
                auto& [bidPrice, bids] = *bids_.begin();
                auto& [askPrice, asks] = *asks_.begin();

                if (bidPrice < askPrice )
                    break;
                
                while (bids.size() && asks.size()) {
                    auto& bid = bids.front();
                    auto& ask = asks.front();
                    
                    Quantity quantity = std::min(bid->GetRemainingQuanity(), ask->GetRemainingQuanity());
                    bid->Fill(quantity);
                    ask->Fill(quantity);

                    if (bid->IsFilled()) {
                        bids.pop_front();
                        orders_.erase(bid->GetOrderID());
                    }

                    if (ask->IsFilled()) {
                        asks.pop_front();
                        orders_.erase(ask->GetOrderID());
                    }

                    //no bids or asks remaining
                    if (bids.empty())
                        bids_.erase(bidPrice);
                    
                    if (asks.empty())
                        asks_.erase(askPrice);

                    TradeInfo bidside = {bid->GetOrderID(), bid->GetPrice(), quantity};
                    TradeInfo sellside = {ask->GetOrderID(), ask->GetPrice(), quantity};
                    
                    trades.push_back(Trade (bidside, sellside));
                }
            }

            if  (!bids_.empty()) {
                auto& [_, bids] = *bids_.begin();
                auto& order = bids.front();
                if (order->GetOrderType() == OrderType::FillandKill) {
                    CancelOrder(order->GetOrderID());
                }
            }

            if (!asks_.empty()) {
                auto& [_, asks] = *asks_.begin();
                auto& order = asks.front();
                if (order->GetOrderType() == OrderType::FillandKill) {
                    CancelOrder(order->GetOrderID());
                }
            }

            return trades;
        }
    
    public:
        Trades AddOrder(OrderPointer order) {
            if (orders_.contains(order->GetOrderID()))
                return { };
            if (order->GetOrderType() == OrderType::FillandKill && !CanMatch(order->GetSide(), order->GetPrice()))
                return { };
            
            OrderPointers::iterator iterator;

            if (order->GetSide() == Side::Buy) {
                auto& orders = bids_[order->GetPrice()];
                orders.push_back(order);
                iterator = std::next(orders.begin(), orders.size() - 1);
            } else {
                auto& orders = asks_[order->GetPrice()];
                orders.push_back(order);
                iterator = std::next(orders.begin(), orders.size() - 1);
            }

            orders_.insert({ order->GetOrderID(), OrderEntry{ order, iterator } });
            return MatchOrders();
        }

        void CancelOrder(OrderID orderId) {
            if (!orders_.contains(orderId))
                return;
            
            auto& [order, iterator] = orders_.at(orderId);
            const auto price = order->GetPrice();
            
            if (order->GetSide() == Side::Sell) {
                auto& orders = asks_.at(price);
                orders.erase(iterator);

                if (orders.empty()) {
                    asks_.erase(price);
                }
            } else {
                auto& orders = bids_.at(price);
                orders.erase(iterator);

                if (orders.empty()) {
                    bids_.erase(price);
                }
            }

            orders_.erase(orderId);
        }


        Trades MatchOrder(OrderModify order) {
            if (orders_.find(order.GetOrderID()) != orders_.end())
                return {};
            
            const auto& [existingOrder, _] = orders_.at(order.GetOrderID());
            CancelOrder(order.GetOrderID());
            return AddOrder(order.ToOrderPointer(existingOrder->GetOrderType()));
        }

        std::size_t Size() const { return orders_.size(); }

        OrderbookLevelInfos GetOrderInfos() const {
            LevelInfos bidInfos, askInfos;
            bidInfos.reserve(orders_.size());
            askInfos.reserve(orders_.size());

            auto CreateLevelInfos = [](Price price, const OrderPointers& orders)
            {
                return LevelInfo{ price, std::accumulate(orders.begin(), orders.end(), (Quantity)0,
                    [](Quantity runningSum, const OrderPointer& order)
                    { return runningSum + order->GetRemainingQuanity(); }) };
            };


            for (const auto& [price, orders]: bids_)
                bidInfos.push_back(CreateLevelInfos(price, orders));

            for (const auto& [price, orders]: asks_)
                askInfos.push_back(CreateLevelInfos(price, orders));

            return OrderbookLevelInfos ( bidInfos, askInfos );
        }
};

int main() {
    Ordebook orderbook;
    const OrderID orderId = 1;
    orderbook.AddOrder(std::make_shared<Order>(OrderType::GoodTillCancel, orderId, Side::Buy, 100, 10));
    std::cout << orderbook.Size() << std::endl; // 1
    orderbook.CancelOrder(orderId);
    std::cout << orderbook.Size() << std::endl; // 0
    return 0;
}