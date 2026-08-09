#pragma once

#include <vector>

#include "Common/AsstTypes.h"
#include "MaaUtils/NoWarningCV.hpp"

namespace asst
{
// 肉鸽商店货架 OCR + 价签模板匹配，识别当前可购买的商品名称。
class RoguelikeTraderGoodsHelper
{
public:
    static std::vector<TextRect> recognize_goods(const cv::Mat& image);
};
}
