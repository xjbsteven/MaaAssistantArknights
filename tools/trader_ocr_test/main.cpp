#include "AsstCaller.h"

#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

#include "MaaUtils/NoWarningCV.hpp"
#include "Task/Roguelike/RoguelikeTraderGoodsHelper.h"

int main(int argc, char** argv)
{
    if (argc < 3) {
        std::cerr << "usage: " << argv[0] << " <resource_dir> <image.png> [image2.png ...]\n";
        return 1;
    }

    const auto resource_dir = std::filesystem::path(argv[1]);
    if (!AsstLoadResource(resource_dir.string().c_str())) {
        std::cerr << "load resource failed: " << resource_dir << '\n';
        return 1;
    }

    for (int i = 2; i < argc; ++i) {
        const auto image_path = std::filesystem::path(argv[i]);
        const cv::Mat image = cv::imread(image_path.string());
        if (image.empty()) {
            std::cerr << "failed to read: " << image_path << '\n';
            continue;
        }

        std::cout << "=== " << image_path.filename().string() << " (" << image.cols << 'x' << image.rows
                  << ") ===\n";
        const auto goods = asst::RoguelikeTraderGoodsHelper::recognize_goods(image);
        std::cout << "recognized " << goods.size() << " goods:\n";
        for (const auto& g : goods) {
            std::cout << "  * " << g.text << '\n';
        }
        std::cout << '\n';
    }

    return 0;
}
