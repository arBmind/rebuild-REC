#pragma once

#include "filter/Token.h"

namespace nesting {

using filter::BlockEndIdentifier;
using filter::BlockStartColon;
using filter::CommentLiteral;
using filter::InvalidEncoding;
using filter::NewLineIndentation;
using filter::SemicolonSeparator;
using filter::UnexpectedCharacter;
using filter::UnexpectedColon;
using filter::WhiteSpaceSeparator;
using UnexpectedIndent = scanner::details::TagErrorToken<struct UnexpectedIndentTag>;
using UnexpectedTokensAfterEnd = scanner::details::TagErrorToken<struct UnexpectedTokensAfterEndTag>;
using UnexpectedBlockEnd = scanner::details::TagErrorToken<struct UnexpectedBlockEndTag>;
using MissingBlockEnd = scanner::details::TagErrorToken<struct MissingBlockEndTag>;
using MisIndentedBlockEnd = scanner::details::TagErrorToken<struct MisIndentedBlockEndTag>;

using Insignificant = meta::Variant<
    CommentLiteral,
    WhiteSpaceSeparator,
    InvalidEncoding,
    UnexpectedCharacter,
    SemicolonSeparator,
    NewLineIndentation,
    BlockStartColon,
    BlockEndIdentifier,
    UnexpectedColon,
    UnexpectedIndent,
    UnexpectedTokensAfterEnd,
    UnexpectedBlockEnd,
    MissingBlockEnd,
    MisIndentedBlockEnd>;

struct Token;

struct BlockLine {
    using This = BlockLine;
    std::vector<Token> tokens{};
    std::vector<Insignificant> insignificants{};

    bool operator==(const This& o) const {
        return tokens == o.tokens //
            && insignificants == o.insignificants;
    }
    bool operator!=(const This& o) const { return !(*this == o); }

    template<class F>
    void forEach(F&& f) const {
        auto ti = tokens.begin();
        auto te = tokens.end();
        auto ii = insignificants.begin();
        auto ie = insignificants.end();
        while (ti != te && ii != ie) {
            auto tv = ti->visit([](auto& x) { return x.input; });
            auto iv = ii->visit([](auto& x) { return x.input; });
            if (tv.begin() < iv.begin())
                f(*ti++);
            else
                f(*ii++);
        }
        while (ti != te) f(*ti++);
        while (ii != ie) f(*ii++);
    }
};
using BlockLines = std::vector<BlockLine>;

struct BlockLiteralValue {
    using This = BlockLiteralValue;
    BlockLines lines{};

    bool operator!=(const This& o) const { return lines != o.lines; }
    bool operator==(const This& o) const { return lines == o.lines; }
};

using BlockLiteral = scanner::details::ValueToken<BlockLiteralValue>;

using ColonSeparator = filter::ColonSeparator;
using CommaSeparator = filter::CommaSeparator;
using SquareBracketOpen = filter::SquareBracketOpen;
using SquareBracketClose = filter::SquareBracketClose;
using BracketOpen = filter::BracketOpen;
using BracketClose = filter::BracketClose;
using StringLiteral = filter::StringLiteral;
using NumberLiteral = filter::NumberLiteral;
using IdentifierLiteral = filter::IdentifierLiteral;
using OperatorLiteral = filter::OperatorLiteral;

using TokenVariant = meta::Variant<
    BlockLiteral,
    ColonSeparator,
    CommaSeparator,
    SquareBracketOpen,
    SquareBracketClose,
    BracketOpen,
    BracketClose,
    StringLiteral,
    NumberLiteral,
    IdentifierLiteral,
    OperatorLiteral>;

struct Token : TokenVariant {
    META_VARIANT_CONSTRUCT(Token, TokenVariant)
};

} // namespace nesting
