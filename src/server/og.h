#ifndef K_OG_H_
#define K_OG_H_

namespace K {
  class OG: public Klass {
    public:
      map<mRandId, mOrder> orders;
      vector<mTrade> tradesHistory;
      function<void(mSide, bool)>* calcWalletAfterOrder = nullptr;
      function<void(mTrade*)>* calcSafetyAfterTrade     = nullptr;
    protected:
      void load() {
        for (json &it : ((DB*)memory)->load(mMatter::Trades))
          tradesHistory.push_back(it);
        ((SH*)screen)->log("DB", string("loaded ") + to_string(tradesHistory.size()) + " historical Trades");
      };
      void waitData() {
        gw->evDataOrder = [&](mOrder k) {                           _debugEvent_
          debug(string("reply  ") + k.orderId + "::" + k.exchangeId + " [" + to_string((int)k.orderStatus) + "]: " + FN::str8(k.quantity) + "/" + FN::str8(k.tradeQuantity) + " at price " + FN::str8(k.price));
          updateOrderState(k);
        };
      };
      void waitUser() {
        ((UI*)client)->welcome(mMatter::Trades, &helloTrades);
        ((UI*)client)->welcome(mMatter::OrderStatusReports, &helloOrders);
        ((UI*)client)->clickme(mMatter::SubmitNewOrder, &kissSubmitNewOrder);
        ((UI*)client)->clickme(mMatter::CancelOrder, &kissCancelOrder);
        ((UI*)client)->clickme(mMatter::CancelAllOrders, &kissCancelAllOrders);
        ((UI*)client)->clickme(mMatter::CleanAllClosedTrades, &kissCleanAllClosedTrades);
        ((UI*)client)->clickme(mMatter::CleanAllTrades, &kissCleanAllTrades);
        ((UI*)client)->clickme(mMatter::CleanTrade, &kissCleanTrade);
      };
      void run() {
        if (((CF*)config)->argDebugOrders) return;
        debug = [](string k) {};
      };
    public:
      void sendOrder(
              vector<mRandId>  toCancel,
        const mSide           &side    ,
        const mPrice          &price   ,
        const mAmount         &qty     ,
        const mOrderType      &type    ,
        const mTimeInForce    &tif     ,
        const bool            &isPong  ,
        const bool            &postOnly
      ) {
        mRandId replaceOrderId;
        if (!toCancel.empty()) {
          replaceOrderId = side == mSide::Bid ? toCancel.back() : toCancel.front();
          toCancel.erase(side == mSide::Bid ? toCancel.end()-1 : toCancel.begin());
          for (mRandId &it : toCancel) cancelOrder(it);
        }
        if (gw->replace and !replaceOrderId.empty()) {
          if (!orders[replaceOrderId].exchangeId.empty()) {
            debug(string(" update") + (side == mSide::Bid ? "BID" : "ASK") + " id " + replaceOrderId + ":  at price " + FN::str8(price) + " " + gw->quote);
            orders[replaceOrderId].price = price;
            gw->replace(orders[replaceOrderId].exchangeId, FN::str8(price));
          }
        } else {
          if (((CF*)config)->argTestChamber != 1) cancelOrder(replaceOrderId);
          mRandId newOrderId = gw->randId();
          updateOrderState(mOrder(
            newOrderId,
            mPair(gw->base, gw->quote),
            side,
            qty,
            type,
            isPong,
            price,
            tif,
            mStatus::New,
            postOnly
          ));
          mOrder *o = &orders[newOrderId];
          debug(string(" send  ") + replaceOrderId + "> " + (o->side == mSide::Bid ? "BID" : "ASK") + " id " + o->orderId + ": " + FN::str8(o->quantity) + " " + o->pair.base + " at price " + FN::str8(o->price) + " " + o->pair.quote);
          gw->place(
            o->orderId,
            o->side,
            FN::str8(o->price),
            FN::str8(o->quantity),
            o->type,
            o->timeInForce,
            o->preferPostOnly,
            o->time
          );
          if (((CF*)config)->argTestChamber == 1) cancelOrder(replaceOrderId);
        }
        ((UI*)client)->orders_60s++;
      };
      void cancelOrder(const mRandId &orderId) {
        if (orderId.empty()) return;
        mOrder *o = &orders[orderId];
        if (o->exchangeId.empty() or o->_waitingCancel + 3e+3 > _Tstamp_) return;
        o->_waitingCancel = _Tstamp_;
        debug(string("cancel ") + (o->side == mSide::Bid ? "BID id " : "ASK id ") + o->orderId + "::" + o->exchangeId);
        gw->cancel(o->orderId, o->exchangeId);
      };
      void cleanOrder(const mRandId &orderId) {
        debug(string("remove ") + orderId);
        map<mRandId, mOrder>::iterator it = orders.find(orderId);
        if (it != orders.end()) orders.erase(it);
      };
    private:
      function<void(json*)> helloTrades = [&](json *welcome) {
        for (mTrade &it : tradesHistory)
          it.loadedFromDB = true;
        *welcome = tradesHistory;
      };
      function<void(json*)> helloOrders = [&](json *welcome) {
        for (map<mRandId, mOrder>::value_type &it : orders)
          if (mStatus::Working == it.second.orderStatus)
            welcome->push_back(it.second);
      };
      function<void(json)> kissCancelAllOrders = [&](json butterfly) {
        cancelOpenOrders();
      };
      function<void(json)> kissCleanAllClosedTrades = [&](json butterfly) {
        cleanClosedTrades();
      };
      function<void(json)> kissCleanAllTrades = [&](json butterfly) {
        cleanTrade();
      };
      function<void(json)> kissCleanTrade = [&](json butterfly) {
        if (butterfly.is_object() and butterfly["tradeId"].is_string())
          cleanTrade(butterfly["tradeId"].get<string>());
      };
      function<void(json)> kissCancelOrder = [&](json butterfly) {
        mRandId orderId = (butterfly.is_object() and butterfly["orderId"].is_string())
          ? butterfly["orderId"].get<mRandId>() : "";
        if (orderId.empty() or orders.find(orderId) == orders.end()) return;
        cancelOrder(orderId);
      };
      function<void(json)> kissSubmitNewOrder = [&](json butterfly) {
        sendOrder(
          vector<mRandId>(),
          butterfly.value("side", "") == "Bid" ? mSide::Bid : mSide::Ask,
          butterfly.value("price", 0.0),
          butterfly.value("quantity", 0.0),
          butterfly.value("orderType", "") == "Limit" ? mOrderType::Limit : mOrderType::Market,
          butterfly.value("timeInForce", "") == "GTC" ? mTimeInForce::GTC : (butterfly.value("timeInForce", "") == "FOK" ? mTimeInForce::FOK : mTimeInForce::IOC),
          false,
          false
        );
      };
      void updateOrderState(mOrder k) {
        bool saved = k.orderStatus != mStatus::New,
             working = k.orderStatus == mStatus::Working;
        if (!saved) orders[k.orderId] = k;
        else if (k.orderId.empty() and !k.exchangeId.empty())
          for (map<mRandId, mOrder>::value_type &it : orders)
            if (k.exchangeId == it.second.exchangeId) {
              k.orderId = it.first;
              break;
            }
        if (k.orderId.empty() or orders.find(k.orderId) == orders.end()) return;
        mOrder *o = &orders[k.orderId];
        o->orderStatus = k.orderStatus;
        if (!k.exchangeId.empty()) o->exchangeId = k.exchangeId;
        if (k.price) o->price = k.price;
        if (k.quantity) o->quantity = k.quantity;
        if (k.time) o->time = k.time;
        if (k.latency) o->latency = k.latency;
        if (!o->time) o->time = _Tstamp_;
        if (!o->latency and working) o->latency = _Tstamp_ - o->time;
        if (o->latency) o->time = _Tstamp_;
        if (k.tradeQuantity)
          toHistory(o, k.tradeQuantity);
        k.side = o->side;
        if (saved and !working)
          cleanOrder(o->orderId);
        else debug(string(" saved ") + (o->side == mSide::Bid ? "BID id " : "ASK id ") + o->orderId + "::" + o->exchangeId + " [" + to_string((int)o->orderStatus) + "]: " + FN::str8(o->quantity) + " " + o->pair.base + " at price " + FN::str8(o->price) + " " + o->pair.quote);
        debug(string("memory ") + to_string(orders.size()));
        if (saved) {
          (*calcWalletAfterOrder)(k.side, k.tradeQuantity > 0);
          toClient(working);
        }
      };
      void cancelOpenOrders() {
        for (map<mRandId, mOrder>::value_type &it : orders)
          if (mStatus::New == it.second.orderStatus or mStatus::Working == it.second.orderStatus)
            cancelOrder(it.first);
      };
      void cleanClosedTrades() {
        for (vector<mTrade>::iterator it = tradesHistory.begin(); it != tradesHistory.end();)
          if (it->Kqty < it->quantity) ++it;
          else {
            it->Kqty = -1;
            ((UI*)client)->send(mMatter::Trades, *it);
            ((DB*)memory)->insert(mMatter::Trades, {}, false, it->tradeId);
            it = tradesHistory.erase(it);
          }
      };
      void cleanTrade(string k = "") {
        bool all = k.empty();
        for (vector<mTrade>::iterator it = tradesHistory.begin(); it != tradesHistory.end();)
          if (!all and it->tradeId != k) ++it;
          else {
            it->Kqty = -1;
            ((UI*)client)->send(mMatter::Trades, *it);
            ((DB*)memory)->insert(mMatter::Trades, {}, false, it->tradeId);
            it = tradesHistory.erase(it);
            if (!all) break;
          }
      };
      inline void toClient(bool working) {
        ((SH*)screen)->log(orders, working);
        json k = json::array();
        for (map<mRandId, mOrder>::value_type &it : orders)
          if (it.second.orderStatus == mStatus::Working)
            k.push_back(it.second);
        ((UI*)client)->send(mMatter::OrderStatusReports, k);
      };
      inline void toHistory(mOrder *o, double tradeQuantity) {
        mAmount fee = 0;
        mTrade trade(
          to_string(_Tstamp_),
          o->pair,
          o->price,
          tradeQuantity,
          o->side,
          o->time,
          abs(o->price * tradeQuantity),
          0, 0, 0, 0, 0, fee, false
        );
        (*calcSafetyAfterTrade)(&trade);
        ((SH*)screen)->log(trade, gw->name);
        if (qp->_matchPings) {
          mPrice widthPong = qp->widthPercentage
            ? qp->widthPongPercentage * trade.price / 100
            : qp->widthPong;
          map<mPrice, string> matches;
          for (mTrade &it : tradesHistory)
            if (it.quantity - it.Kqty > 0
              and it.side != trade.side
              and (qp->pongAt == mPongAt::AveragePingFair
                or qp->pongAt == mPongAt::AveragePingAggressive
                or (trade.side == mSide::Bid
                  ? (it.price > trade.price + widthPong)
                  : (it.price < trade.price - widthPong)
                )
              )
            ) matches[it.price] = it.tradeId;
          matchPong(
            matches,
            trade,
            (qp->pongAt == mPongAt::LongPingFair or qp->pongAt == mPongAt::LongPingAggressive) ? trade.side == mSide::Ask : trade.side == mSide::Bid
          );
        } else {
          ((UI*)client)->send(mMatter::Trades, trade);
          ((DB*)memory)->insert(mMatter::Trades, trade, false, trade.tradeId);
          tradesHistory.push_back(trade);
        }
        ((UI*)client)->send(mMatter::TradesChart, {
          {"price", trade.price},
          {"side", trade.side},
          {"quantity", trade.quantity},
          {"value", trade.value},
          {"pong", o->isPong}
        });
        if (qp->cleanPongsAuto) cleanAuto(trade.time);
      };
      void matchPong(map<mPrice, string> matches, mTrade pong, bool reverse) {
        if (reverse) for (map<mPrice, string>::reverse_iterator it = matches.rbegin(); it != matches.rend(); ++it) {
          if (!matchPong(it->second, &pong)) break;
        } else for (map<mPrice, string>::iterator it = matches.begin(); it != matches.end(); ++it)
          if (!matchPong(it->second, &pong)) break;
        if (pong.quantity > 0) {
          bool eq = false;
          for (mTrade &it : tradesHistory) {
            if (it.price!=pong.price or it.side!=pong.side or it.quantity<=it.Kqty) continue;
            eq = true;
            it.time = pong.time;
            it.quantity = it.quantity + pong.quantity;
            it.value = it.value + pong.value;
            it.loadedFromDB = false;
            ((UI*)client)->send(mMatter::Trades, it);
            ((DB*)memory)->insert(mMatter::Trades, it, false, it.tradeId);
            break;
          }
          if (!eq) {
            ((UI*)client)->send(mMatter::Trades, pong);
            ((DB*)memory)->insert(mMatter::Trades, pong, false, pong.tradeId);
            tradesHistory.push_back(pong);
          }
        }
      };
      bool matchPong(string match, mTrade* pong) {
        for (mTrade &it : tradesHistory) {
          if (it.tradeId != match) continue;
          mAmount Kqty = fmin(pong->quantity, it.quantity - it.Kqty);
          it.Ktime = pong->time;
          it.Kprice = ((Kqty*pong->price) + (it.Kqty*it.Kprice)) / (it.Kqty+Kqty);
          it.Kqty = it.Kqty + Kqty;
          it.Kvalue = abs(it.Kqty*it.Kprice);
          pong->quantity = pong->quantity - Kqty;
          pong->value = abs(pong->price*pong->quantity);
          if (it.quantity<=it.Kqty)
            it.Kdiff = abs(it.quantity * it.price - it.Kqty * it.Kprice);
          it.loadedFromDB = false;
          ((UI*)client)->send(mMatter::Trades, it);
          ((DB*)memory)->insert(mMatter::Trades, it, false, it.tradeId);
          break;
        }
        return pong->quantity > 0;
      };
      void cleanAuto(mClock now) {
        mClock pT_ = now - (abs(qp->cleanPongsAuto) * 864e5);
        for (vector<mTrade>::iterator it = tradesHistory.begin(); it != tradesHistory.end();)
          if (it->time < pT_ and (qp->cleanPongsAuto < 0 or it->Kqty >= it->quantity)) {
            it->Kqty = -1;
            ((UI*)client)->send(mMatter::Trades, *it);
            ((DB*)memory)->insert(mMatter::Trades, {}, false, it->tradeId);
            it = tradesHistory.erase(it);
          } else ++it;
      };
      function<void(string)> debug = [&](string k) {
        ((SH*)screen)->log("DEBUG", string("OG ") + k);
      };
  };
}

#endif
