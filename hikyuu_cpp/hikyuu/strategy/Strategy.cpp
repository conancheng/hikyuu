/*
 *  Copyright(C) 2021 hikyuu.org
 *
 *  Create on: 2021-02-16
 *     Author: fasiondog
 */

#include <csignal>
#include <unordered_set>
#include "hikyuu/utilities/os.h"
#include "hikyuu/utilities/ini_parser/IniParser.h"
#include "hikyuu/utilities/node/NodeClient.h"
#include "hikyuu/global/GlobalSpotAgent.h"
#include "hikyuu/global/schedule/scheduler.h"
#include "hikyuu/global/sysinfo.h"
#include "hikyuu/hikyuu.h"
#include "Strategy.h"

namespace hku {

std::atomic_bool Strategy::ms_keep_running = true;

void Strategy::sig_handler(int sig) {
    if (sig == SIGINT) {
        ms_keep_running = false;
        exit(0);
    }
}

Strategy::Strategy() : Strategy("Strategy", "") {}

Strategy::Strategy(const string& name, const string& config_file)
: m_name(name), m_config_file(config_file) {
    if (m_config_file.empty()) {
        string home = getUserDir();
        HKU_ERROR_IF(home == "", "Failed get user home path!");
#if HKU_OS_WINOWS
        m_config_file = format("{}\\{}", home, ".hikyuu\\hikyuu.ini");
#else
        m_config_file = format("{}/{}", home, ".hikyuu/hikyuu.ini");
#endif
    }
}

Strategy::Strategy(const vector<string>& codeList, const vector<KQuery::KType>& ktypeList,
                   const string& name, const string& config_file)
: Strategy(name, config_file) {
    m_context.setStockCodeList(codeList);
    m_context.setKTypeList(ktypeList);
}

Strategy::Strategy(const StrategyContext& context, const string& name, const string& config_file)
: Strategy(name, config_file) {
    m_context = m_context;
}

Strategy::~Strategy() {
    ms_keep_running = false;
    CLS_INFO("Quit Strategy {}!", m_name);
}

void Strategy::_init() {
    StockManager& sm = StockManager::instance();

    // sm 尚未初始化，则初始化
    if (sm.thread_id() == std::thread::id()) {
        // 注册 ctrl-c 终止信号
        std::signal(SIGINT, sig_handler);

        CLS_INFO("{} is running! You can press Ctrl-C to terminte ...", m_name);

        // 初始化
        hikyuu_init(m_config_file, false, m_context);

        // 对上下文中的K线类型和其预加载参数进行检查，并给出警告
        const auto& ktypes = m_context.getKTypeList();
        const auto& preload_params = sm.getPreloadParameter();
        for (const auto& ktype : ktypes) {
            std::string low_ktype = ktype;
            to_lower(low_ktype);
            HKU_ERROR_IF(!preload_params.tryGet(low_ktype, false),
                         "The K-line type in the context is not configured to be preloaded!");
        }

    } else {
        m_context = sm.getStrategyContext();
    }

    CLS_CHECK(!m_context.getStockCodeList().empty(), "The context does not contain any stocks!");
    CLS_CHECK(!m_context.getKTypeList().empty(), "The K type list was empty!");

    // 先将行情接收代理停止，以便后面加入处理函数
    stopSpotAgent();
}

void Strategy::start(bool autoRecieveSpot) {
    _init();

    _runDailyAt();

    if (autoRecieveSpot) {
        size_t stock_num = StockManager::instance().size();
        size_t spot_worker_num = stock_num / 300;
        size_t cpu_num = std::thread::hardware_concurrency();
        if (spot_worker_num > cpu_num) {
            spot_worker_num = cpu_num;
        }

        auto& agent = *getGlobalSpotAgent();
        agent.addProcess([this](const SpotRecord& spot) { _receivedSpot(spot); });
        agent.addPostProcess([this](Datetime revTime) {
            if (m_on_recieved_spot) {
                event([=]() { m_on_recieved_spot(revTime); });
            }
        });
        startSpotAgent(true, spot_worker_num);
    }

    _runDaily();

    CLS_INFO("start even loop ...");
    _startEventLoop();
}

void Strategy::onChange(std::function<void(const Stock&, const SpotRecord& spot)>&& changeFunc) {
    HKU_CHECK(changeFunc, "Invalid changeFunc!");
    m_on_change = changeFunc;
}

void Strategy::onReceivedSpot(std::function<void(const Datetime&)>&& recievedFucn) {
    HKU_CHECK(recievedFucn, "Invalid recievedFucn!");
    m_on_recieved_spot = recievedFucn;
}

void Strategy::_receivedSpot(const SpotRecord& spot) {
    Stock stk = getStock(format("{}{}", spot.market, spot.code));
    if (!stk.isNull()) {
        if (m_on_change) {
            event([=]() { m_on_change(stk, spot); });
        }
    }
}

void Strategy::runDaily(std::function<void()>&& func, const TimeDelta& delta,
                        const std::string& market, bool ignoreMarket) {
    HKU_CHECK(func, "Invalid func!");
    m_run_daily_delta = delta;
    m_run_daily_market = market;
    m_ignoreMarket = ignoreMarket;

    if (ignoreMarket) {
        m_run_daily_func = [=]() { event(func); };

    } else {
        m_run_daily_func = [=]() {
            const auto& sm = StockManager::instance();
            auto today = Datetime::today();
            int day = today.dayOfWeek();
            if (day == 0 || day == 6 || sm.isHoliday(today)) {
                return;
            }

            auto market_info = sm.getMarketInfo(m_run_daily_market);
            Datetime open1 = today + market_info.openTime1();
            Datetime close1 = today + market_info.closeTime1();
            Datetime open2 = today + market_info.openTime2();
            Datetime close2 = today + market_info.closeTime2();
            Datetime now = Datetime::now();
            if ((now >= open1 && now <= close1) || (now >= open2 && now <= close2)) {
                event(func);
            }
        };
    }
}

void Strategy::_runDaily() {
    HKU_IF_RETURN(!m_run_daily_func, void());

    auto* scheduler = getScheduler();
    if (m_ignoreMarket) {
        scheduler->addDurationFunc(std::numeric_limits<int>::max(), m_run_daily_delta,
                                   m_run_daily_func);
        return;
    }

    try {
        const auto& sm = StockManager::instance();
        auto market_info = sm.getMarketInfo(m_run_daily_market);
        auto today = Datetime::today();
        auto now = Datetime::now();
        TimeDelta now_time = now - today;
        if (now_time >= market_info.closeTime2()) {
            scheduler->addFuncAtTime(today.nextDay() + market_info.openTime1(), [=]() {
                m_run_daily_func();
                auto* sched = getScheduler();
                sched->addDurationFunc(std::numeric_limits<int>::max(), m_run_daily_delta,
                                       m_run_daily_func);
            });

        } else if (now_time >= market_info.openTime2()) {
            int64_t ticks = now_time.ticks() - market_info.openTime2().ticks();
            int64_t delta_ticks = m_run_daily_delta.ticks();
            if (ticks % delta_ticks == 0) {
                scheduler->addDurationFunc(std::numeric_limits<int>::max(), m_run_daily_delta,
                                           m_run_daily_func);
            } else {
                auto delay = TimeDelta::fromTicks((ticks / delta_ticks + 1) * delta_ticks - ticks);
                scheduler->addFuncAtTime(now + delay, [=]() {
                    m_run_daily_func();
                    auto* sched = getScheduler();
                    sched->addDurationFunc(std::numeric_limits<int>::max(), m_run_daily_delta,
                                           m_run_daily_func);
                });
            }

        } else if (now_time >= market_info.closeTime1()) {
            scheduler->addFuncAtTime(today + market_info.openTime2(), [=]() {
                m_run_daily_func();
                auto* sched = getScheduler();
                sched->addDurationFunc(std::numeric_limits<int>::max(), m_run_daily_delta,
                                       m_run_daily_func);
            });

        } else if (now_time < market_info.closeTime1() && now_time >= market_info.openTime1()) {
            int64_t ticks = now_time.ticks() - market_info.openTime1().ticks();
            int64_t delta_ticks = m_run_daily_delta.ticks();
            if (ticks % delta_ticks == 0) {
                scheduler->addDurationFunc(std::numeric_limits<int>::max(), m_run_daily_delta,
                                           m_run_daily_func);
            } else {
                auto delay = TimeDelta::fromTicks((ticks / delta_ticks + 1) * delta_ticks - ticks);
                scheduler->addFuncAtTime(now + delay, [=]() {
                    m_run_daily_func();
                    auto* sched = getScheduler();
                    sched->addDurationFunc(std::numeric_limits<int>::max(), m_run_daily_delta,
                                           m_run_daily_func);
                });
            }

        } else if (now_time < market_info.openTime1()) {
            scheduler->addFuncAtTime(today + market_info.openTime1(), [=]() {
                m_run_daily_func();
                auto* sched = getScheduler();
                sched->addDurationFunc(std::numeric_limits<int>::max(), m_run_daily_delta,
                                       m_run_daily_func);
            });

        } else {
            CLS_ERROR("Unknown process! now_time: {}", now_time);
        }
    } catch (const std::exception& e) {
        CLS_THROW(e.what());
    }
}

void Strategy::runDailyAt(std::function<void()>&& func, const TimeDelta& delta,
                          bool ignoreHoliday) {
    HKU_CHECK(func, "Invalid func!");
    m_run_daily_at_delta = delta;

    if (ignoreHoliday) {
        m_run_daily_at_func = [=]() {
            const auto& sm = StockManager::instance();
            auto today = Datetime::today();
            int day = today.dayOfWeek();
            if (day != 0 && day != 6 && !sm.isHoliday(today)) {
                event(func);
            }
        };

    } else {
        m_run_daily_at_func = [=]() { event(func); };
    }
}

void Strategy::_runDailyAt() {
    if (m_run_daily_at_func) {
        auto* scheduler = getScheduler();
        scheduler->addFuncAtTimeEveryDay(m_run_daily_at_delta, m_run_daily_at_func);
    }
}

/*
 * 在主线程中处理事件队列，避免 python GIL
 */
void Strategy::_startEventLoop() {
    while (ms_keep_running) {
        event_type task;
        m_event_queue.wait_and_pop(task);
        if (task.isNullTask()) {
            ms_keep_running = false;
        } else {
            try {
                task();
            } catch (const std::exception& e) {
                CLS_ERROR("Failed run task! {}", e.what());
            } catch (...) {
                CLS_ERROR("Failed run task! Unknow error!");
            }
        }
    }
}

void HKU_API runInStrategy(const SYSPtr& sys, const Stock& stk, const KQuery& query,
                           const OrderBrokerPtr& broker, const TradeCostPtr& costfunc) {
    HKU_ASSERT(sys && broker && sys->getTM());
    HKU_ASSERT(!stk.isNull());
    HKU_ASSERT(query != Null<KQuery>());
    HKU_CHECK(!sys->getParam<bool>("buy_delay") && !sys->getParam<bool>("sell_delay"),
              "Thie method only support buy|sell on close!");

    auto tm = crtBrokerTM(broker, costfunc, sys->name());
    tm->fetchAssetInfoFromBroker(broker);
    sys->setTM(tm);
    sys->setSP(SlippagePtr());  // 清除移滑价差算法
    sys->run(stk, query);
}

void HKU_API runInStrategy(const PFPtr& pf, const KQuery& query, int adjust_cycle,
                           const OrderBrokerPtr& broker, const TradeCostPtr& costfunc) {
    HKU_ASSERT(pf && broker && pf->getTM());
    HKU_ASSERT(query != Null<KQuery>());

    auto se = pf->getSE();
    HKU_ASSERT(se);
    const auto& sys_list = se->getProtoSystemList();
    for (const auto& sys : sys_list) {
        HKU_CHECK(!sys->getSP(), "Exist Slippage part in sys, You must clear it! {}", sys->name());
        HKU_CHECK(!sys->getParam<bool>("buy_delay") && !sys->getParam<bool>("sell_delay"),
                  "Thie method only support buy|sell on close!");
    }

    auto tm = crtBrokerTM(broker, costfunc, pf->name());
    tm->fetchAssetInfoFromBroker(broker);
    pf->setTM(tm);
    pf->run(query, adjust_cycle, true);
}

void HKU_API getDataFromBufferServer(const std::string& addr, const StockList& stklist,
                                     const KQuery::KType& ktype) {
    // SPEND_TIME(getDataFromBufferServer);
    const auto& preload = StockManager::instance().getPreloadParameter();
    string low_ktype = ktype;
    to_lower(low_ktype);
    HKU_ERROR_IF_RETURN(!preload.tryGet<bool>(low_ktype, false), void(),
                        "The {} kdata is not preload! Can't update!", low_ktype);

    NodeClient client(addr);
    try {
        HKU_CHECK(client.dial(), "Failed dial server!");
        json req;
        req["cmd"] = "market";
        req["ktype"] = ktype;
        json code_list;
        for (const auto& stk : stklist) {
            code_list.emplace_back(stk.market_code());
        }
        req["codes"] = std::move(code_list);

        json res;
        client.post(req, res);
        HKU_ERROR_IF_RETURN(res["ret"] != NodeErrorCode::SUCCESS, void(),
                            "Recieved error: {}, msg: {}", res["ret"].get<int>(),
                            res["msg"].get<string>());

        const auto& jdata = res["data"];
        // HKU_INFO("{}", to_string(jdata));
        for (auto iter = jdata.cbegin(); iter != jdata.cend(); ++iter) {
            const auto& r = *iter;
            try {
                string market_code = r[0].get<string>();
                Stock stk = getStock(market_code);
                if (!stk.isNull()) {
                    KRecord k(Datetime(r[1].get<string>()), r[2], r[3], r[4], r[5], r[6], r[7]);
                    stk.realtimeUpdate(k, ktype);
                }
            } catch (const std::exception& e) {
                HKU_ERROR("Failed decode json: {}! {}", to_string(r), e.what());
            }
        }

    } catch (const std::exception& e) {
        HKU_ERROR("Failed get data from buffer server! {}", e.what());
    } catch (...) {
        HKU_ERROR_UNKNOWN;
    }
}

}  // namespace hku