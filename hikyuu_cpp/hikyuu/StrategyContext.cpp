/*
 *  Copyright(C) 2021 hikyuu.org
 *
 *  Create on: 2021-02-10
 *     Author: fasiondog
 */

#include "StrategyContext.h"

namespace hku {

void StrategyContext::setStockCodeList(const vector<string>& stockList) {
    m_stockCodeList.resize(stockList.size());
    std::copy(stockList.begin(), stockList.end(), m_stockCodeList.begin());
}

void StrategyContext::setKTypeList(const vector<KQuery::KType>& ktypeList) {
    m_ktypeList.resize(ktypeList.size());
    std::transform(ktypeList.begin(), ktypeList.end(), m_ktypeList.begin(),
                   [](KQuery::KType ktype) {
                       to_upper(ktype);
                       return ktype;
                   });
}

bool StrategyContext::isAll() const noexcept {
    return std::find_if(m_stockCodeList.begin(), m_stockCodeList.end(), [](string val) {
               to_upper(val);
               return val == "ALL";
           }) != m_stockCodeList.end();
}

vector<string> StrategyContext::getAllNeedLoadStockCodeList() const {
    vector<string> ret{m_stockCodeList};
    for (const auto& code : m_mustLoad) {
        ret.push_back(code);
    }
    return ret;
}

}  // namespace hku