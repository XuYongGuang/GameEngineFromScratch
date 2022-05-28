#include "CodeGenerator.hpp"

using namespace My;

// public
void CodeGenerator::GenerateCode(std::ostream& out, const ASTNodeRef& ref, CODE_GENERATION_TYPE type) {
    switch (type) {
        case CODE_GENERATION_TYPE::GRAPHVIZ_DOT:
            generateGraphvizDot(out, ref);
            break;
        case CODE_GENERATION_TYPE::CPP_HEADER:
            generateCppSnippet(out, ref);
            break;
        default:
            assert(0);
    }
}
