#include "AsstCaller.h"

#include <filesystem>
#include <iostream>
#include <string>

#include "MaaUtils/NoWarningCV.hpp"
#include "Vision/MultiMatcher.h"
#include "Vision/OCRer.h"

int main(int argc, char** argv)
{
    if (argc < 3) {
        std::cerr << "usage: " << argv[0] << " <resource_dir> <image.png>\n";
        return 1;
    }

    if (!AsstLoadResource(std::filesystem::path(argv[1]).string().c_str())) {
        return 1;
    }

    const cv::Mat image = cv::imread(argv[2]);
    if (image.empty()) {
        return 1;
    }

    asst::OCRer analyzer(image);
    analyzer.set_task_info("Roguelike@GetDropSelectCollectibleOcr");
    if (!analyzer.analyze()) {
        std::cout << "OCR empty\n";
        return 0;
    }

    for (const auto& item : analyzer.get_result()) {
        std::cout << item.text << " @" << item.rect.to_string() << '\n';
    }

    asst::MultiMatcher buttons(image);
    buttons.set_task_info("Mizuki@Roguelike@GetDropSelectReward");
    if (buttons.analyze()) {
        std::cout << "--- buttons ---\n";
        for (const auto& b : buttons.get_result()) {
            std::cout << b.rect.to_string() << " score=" << b.score << '\n';
        }
    }

    return 0;
}
