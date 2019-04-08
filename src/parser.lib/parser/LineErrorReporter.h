#pragma once
#include "Context.h"

#include "nesting/Token.h"

#include "strings/Rope.h"
#include "strings/utf8Decode.h"

#include <bitset>
#include <sstream>

namespace parser {

template<class Context>
void reportLineErrors(const nesting::BlockLine& line, Context& context) {
    line.forEach([&](auto& t) {
        t.visitSome(
            [&](const nesting::NewLineIndentation& nli) { reportNewline(line, nli, context); },
            [&](const nesting::CommentLiteral& cl) { reportTokenWithDecodeErrors(line, cl, context); },
            [&](const nesting::StringLiteral& sl) { reportStringLiteral(line, sl, context); },
            [&](const nesting::NumberLiteral& sl) { reportNumberLiteral(line, sl, context); },
            [&](const nesting::IdentifierLiteral& il) { reportTokenWithDecodeErrors(line, il, context); },
            [&](const nesting::OperatorLiteral& ol) { reportOperatorLiteral(line, ol, context); },
            [&](const nesting::InvalidEncoding& ie) { reportInvalidEncoding(line, ie, context); },
            [&](const nesting::UnexpectedCharacter& uc) { reportUnexpectedCharacter(line, uc, context); }

        );
    });
}

inline auto extractBlockLines(const nesting::BlockLine& blockLine) -> strings::View {
    auto begin = strings::View::It{};
    auto end = strings::View::It{};
    if (!blockLine.tokens.empty()) {
        begin = blockLine.tokens.front().visit([](auto& t) { return t.input.begin(); });
        end = blockLine.tokens.back().visit([](auto& t) { return t.input.end(); });
    }
    if (!blockLine.insignificants.empty()) {
        auto begin2 = blockLine.insignificants.front().visit([](auto& t) { return t.input.begin(); });
        auto end2 = blockLine.insignificants.back().visit([](auto& t) { return t.input.end(); });
        if (!begin || begin2 < begin) begin = begin2;
        if (!end || end2 > end) end = end2;
    }
    return strings::View{begin, end};
}

// extends the view so that it starts after a newline and ends before a newline
// note: it will never expand beyond the current blockLine - as we risk to run before the start of the string
inline auto extractViewLines(const nesting::BlockLine& blockLine, strings::View view) -> strings::View {
    auto all = extractBlockLines(blockLine);
    auto begin = view.begin();
    while (begin > all.begin() && begin[-1] != '\r' && begin[-1] != '\n') begin--;
    auto end = view.end();
    while (end < all.end() && end[0] != '\r' && end[0] != '\n') end++;
    return strings::View{begin, end};
}

using ViewMarkers = std::vector<strings::View>;

struct EscapedMarkers {
    strings::String escaped;
    diagnostic::TextSpans markers;
};

inline auto escapeSourceLine(strings::View view, ViewMarkers viewMarkers) -> EscapedMarkers {
    auto output = strings::Rope{};
    auto markers = diagnostic::TextSpans{};
    markers.resize(viewMarkers.size(), diagnostic::TextSpan{-1, -1});
    auto begin = view.begin();
    auto offset = 0;
    auto updateMarkers = [&](strings::View::It p) {
        auto i = 0;
        for (const auto& vm : viewMarkers) {
            auto& m = markers[i];
            if (vm.begin() <= p && m.start == -1) m.start = offset;
            if (vm.end() <= p && m.length == -1) m.length = offset - m.start;
            i++;
        }
    };
    auto requiresEscapes = false;
    auto addEscaped = [&](strings::View input) {
        output += strings::View{begin, input.begin()};
        auto escaped = std::stringstream{};
        if (input.size() == 1) {
            switch (input.front()) {
            case 0xA: escaped << "\\n\n"; break;
            case 0xD:
                requiresEscapes = true;
                escaped << R"(\r)";
                break;
            case 0x9:
                requiresEscapes = true;
                escaped << R"(\t)";
                break;
            case 0x0:
                requiresEscapes = true;
                escaped << R"(\0)";
                break;
            default:
                requiresEscapes = true;
                escaped << R"(\[)" << std::hex << ((int)input.front() & 255) << "]" << std::dec;
                break;
            }
        }
        else {
            requiresEscapes = true;
            escaped << R"(\[)" << std::hex;
            for (auto x : input) escaped << ((int)x & 255);
            escaped << "]" << std::dec;
        }
        auto str = escaped.str();
        output += strings::String{str.data(), str.data() + str.size()};
        begin = input.end();
        offset += str.length();
    };

    for (auto d : strings::utf8Decode(view)) {
        d.visit(
            [&](strings::DecodedCodePoint dcp) {
                updateMarkers(dcp.input.begin());
                if (dcp.cp.isCombiningMark() || dcp.cp.isControl() || dcp.cp.isNonCharacter() || dcp.cp.isSurrogate()) {
                    addEscaped(dcp.input);
                    return;
                }
                else if (dcp.cp.v == '\\') {
                    output += strings::View{begin, dcp.input.end()};
                    output += dcp.cp;
                    begin = dcp.input.end();
                    offset += 1;
                }
                offset += 1;
            },
            [&](strings::DecodedError de) {
                updateMarkers(de.input.begin());
                addEscaped(de.input);
            });
    }
    output += strings::View{begin, view.end()};
    updateMarkers(view.end());

    if (!requiresEscapes) { // do not escape if not necessary
        auto i = 0;
        for (const auto& vm : viewMarkers) {
            auto& m = markers[i];
            m.start = vm.begin() - view.begin();
            m.length = vm.byteCount().v;
        }
        return EscapedMarkers{to_string(view), std::move(markers)};
    }

    return EscapedMarkers{to_string(output), std::move(markers)};
}

template<class Context>
void reportDecodeErrorMarkers(
    text::Line line,
    strings::View tokenLines,
    const parser::ViewMarkers& viewMarkers,
    parser::ContextApi<Context>& context) {

    using namespace diagnostic;

    auto [escapedLines, escapedMarkers] = escapeSourceLine(tokenLines, viewMarkers);

    auto highlights = Highlights{};
    for (auto& m : escapedMarkers) highlights.emplace_back(Marker{m, {}});

    auto doc = Document{
        {Paragraph{(viewMarkers.size() == 1) ? String{"The UTF8-decoder encountered an invalid encoding"}
                                             : String{"The UTF8-decoder encountered multiple invalid encodings"},
                   {}},
         SourceCodeBlock{escapedLines, highlights, String{}, line}}};

    auto expl = Explanation{String("Invalid UTF8 Encoding"), doc};

    auto d = Diagnostic{Code{String{"rebuild-lexer"}, 1}, Parts{expl}};
    context.reportDiagnostic(std::move(d));
}

inline void collectDecodeErrorMarkers(
    ViewMarkers& viewMarkers, const nesting::BlockLine& blockLine, const strings::View& tokenLines, const void* tok) {
    auto isOnLine = [&](strings::View& input) {
        return input.begin() >= tokenLines.begin() && input.end() <= tokenLines.end();
    };
    for (auto& t : blockLine.insignificants) {
        t.visitSome(
            [&](const nesting::InvalidEncoding& ie) {
                if (ie.isTainted || !ie.input.isPartOf(tokenLines)) return;
                viewMarkers.emplace_back(ie.input);
                if (&ie != tok) const_cast<nesting::InvalidEncoding&>(ie).isTainted = true;
            },
            [&](const nesting::CommentLiteral& cl) {
                if (cl.isTainted || !cl.input.isPartOf(tokenLines)) return;
                for (auto& p : cl.decodeErrors) viewMarkers.emplace_back(p.input);
                if (&cl != tok) const_cast<nesting::CommentLiteral&>(cl).isTainted = true;
            },
            [&](const nesting::IdentifierLiteral& il) {
                if (il.isTainted || !il.input.isPartOf(tokenLines)) return;
                for (auto& p : il.decodeErrors) viewMarkers.emplace_back(p.input);
                if (&il != tok) const_cast<nesting::IdentifierLiteral&>(il).isTainted = true;
            },
            [&](const nesting::NewLineIndentation& nli) {
                if (nli.isTainted || !nli.input.isPartOf(tokenLines)) return;
                for (auto& err : nli.value.errors) {
                    if (!err.holds<scanner::DecodedErrorPosition>()) return;
                }
                for (auto& err : nli.value.errors) {
                    err.visitSome(
                        [&](const scanner::DecodedErrorPosition& dep) { viewMarkers.emplace_back(dep.input); });
                }
                if (&nli != tok) const_cast<nesting::NewLineIndentation&>(nli).isTainted = true;
            });
    }
}

template<class Token, class Context>
void reportDecodeErrors(const nesting::BlockLine& blockLine, const Token& tok, ContextApi<Context>& context) {
    using namespace diagnostic;

    auto tokenLines = extractViewLines(blockLine, tok.input);
    auto viewMarkers = ViewMarkers{};
    collectDecodeErrorMarkers(viewMarkers, blockLine, tokenLines, &tok);
    reportDecodeErrorMarkers(tok.position.line, tokenLines, viewMarkers, context);
}

template<class... Tags, class Context>
void reportTokenWithDecodeErrors(
    const nesting::BlockLine& blockLine,
    const scanner::details::TagTokenWithDecodeErrors<Tags...>& de,
    ContextApi<Context>& context) {
    if (de.isTainted || de.decodeErrors.empty()) return; // already reported or no errors

    reportDecodeErrors(blockLine, de, context);
}

template<class Context>
void reportInvalidEncoding(
    const nesting::BlockLine& blockLine, const nesting::InvalidEncoding& ie, ContextApi<Context>& context) {
    if (ie.isTainted) return; // already reported

    reportDecodeErrors(blockLine, ie, context);
}

template<class Context>
void reportNewline(
    const nesting::BlockLine& blockLine, const nesting::NewLineIndentation& nli, ContextApi<Context>& context) {
    if (nli.isTainted || !nli.value.hasErrors()) return; // already reported or no errors

    using namespace diagnostic;

    auto tokenLines = extractViewLines(blockLine, nli.input);
    {
        auto viewMarkers = ViewMarkers{};
        for (auto& err : nli.value.errors) {
            err.visitSome([&](const scanner::DecodedErrorPosition& dep) { viewMarkers.emplace_back(dep.input); });
        }
        if (!viewMarkers.empty()) {
            if (viewMarkers.size() == nli.value.errors.size()) viewMarkers.clear();
            collectDecodeErrorMarkers(viewMarkers, blockLine, tokenLines, &nli);
            reportDecodeErrorMarkers(text::Line{nli.position.line.v - 1}, tokenLines, viewMarkers, context);
        }
    }
    {
        auto viewMarkers = ViewMarkers{};
        for (auto& err : nli.value.errors) {
            err.visitSome([&](const scanner::MixedIndentCharacter& mic) { viewMarkers.emplace_back(mic.input); });
        }
        if (viewMarkers.empty()) return;

        for (auto& t : blockLine.insignificants) {
            t.visitSome([&](const nesting::NewLineIndentation& onli) {
                if (onli.isTainted || !onli.input.isPartOf(tokenLines)) return;
                for (auto& err : onli.value.errors) {
                    if (!err.holds<scanner::MixedIndentCharacter>()) return;
                }
                for (auto& err : onli.value.errors) {
                    err.visitSome(
                        [&](const scanner::MixedIndentCharacter& mic) { viewMarkers.emplace_back(mic.input); });
                }
                if (&onli != (void*)&nli) const_cast<nesting::NewLineIndentation&>(onli).isTainted = true;
            });
        }

        auto [escapedLines, escapedMarkers] = escapeSourceLine(tokenLines, viewMarkers);

        auto highlights = Highlights{};
        for (auto& m : escapedMarkers) highlights.emplace_back(Marker{m, {}});

        auto doc = Document{{Paragraph{String{"The indentation mixes tabs and spaces."}, {}},
                             SourceCodeBlock{escapedLines, highlights, String{}, text::Line{nli.position.line.v - 1}}}};

        auto expl = Explanation{String("Mixed Indentation Characters"), doc};

        auto d = Diagnostic{Code{String{"rebuild-lexer"}, 3}, Parts{expl}};
        context.reportDiagnostic(std::move(d));
    }
}

template<class Context>
void reportUnexpectedCharacter(
    const nesting::BlockLine& blockLine, const nesting::UnexpectedCharacter& uc, ContextApi<Context>& context) {
    if (uc.isTainted) return;

    using namespace diagnostic;

    auto tokenLines = extractViewLines(blockLine, uc.input);

    auto viewMarkers = ViewMarkers{};
    for (auto& t : blockLine.insignificants) {
        t.visitSome([&](const nesting::UnexpectedCharacter& ouc) {
            if (ouc.input.begin() >= tokenLines.begin() && ouc.input.end() <= tokenLines.end()) {
                viewMarkers.emplace_back(ouc.input);
                if (&ouc != (void*)&uc) const_cast<nesting::UnexpectedCharacter&>(ouc).isTainted = true;
            }
        });
    }

    auto [escapedLines, escapedMarkers] = escapeSourceLine(tokenLines, viewMarkers);

    auto highlights = Highlights{};
    for (auto& m : escapedMarkers) highlights.emplace_back(Marker{m, {}});

    auto doc = Document{
        {Paragraph{(viewMarkers.size() == 1)
                       ? String{"The tokenizer encountered a character that is not part of any Rebuild language token."}
                       : String{"The tokenizer encountered multiple characters that are not part of any Rebuild "
                                "language token."},
                   {}},
         SourceCodeBlock{escapedLines, highlights, String{}, uc.position.line}}};

    auto expl = Explanation{String("Unexpected characters"), doc};

    auto d = Diagnostic{Code{String{"rebuild-lexer"}, 2}, Parts{expl}};
    context.reportDiagnostic(std::move(d));
}

template<class Context>
void reportStringLiteral(
    const nesting::BlockLine& blockLine, const nesting::StringLiteral& sl, ContextApi<Context>& context) {
    if (sl.isTainted || !sl.value.hasErrors()) return;

    using namespace diagnostic;

    auto tokenLines = extractViewLines(blockLine, sl.input);

    auto reportedKinds = std::bitset<8>{};
    for (auto& err : sl.value.errors) {
        if (reportedKinds[static_cast<int>(err.kind)]) continue;
        reportedKinds.set(static_cast<int>(err.kind));

        auto viewMarkers = ViewMarkers{};
        for (auto& err2 : sl.value.errors)
            if (err2.kind == err.kind) viewMarkers.emplace_back(err2.input);

        auto [escapedLines, escapedMarkers] = escapeSourceLine(tokenLines, viewMarkers);

        auto highlights = Highlights{};
        for (auto& m : escapedMarkers) highlights.emplace_back(Marker{m, {}});

        using Kind = scanner::StringError::Kind;
        switch (err.kind) {
        case Kind::EndOfInput: {
            auto doc = Document{{Paragraph{String{"The string was not terminated."}, {}},
                                 SourceCodeBlock{escapedLines, highlights, String{}, sl.position.line}}};
            auto expl = Explanation{String("Unexpected end of input"), doc};
            auto d = Diagnostic{Code{String{"rebuild-lexer"}, 10}, Parts{expl}};
            context.reportDiagnostic(std::move(d));
            break;
        }
        case Kind::InvalidEncoding: {
            reportDecodeErrorMarkers(sl.position.line, tokenLines, viewMarkers, context);
            break;
        }
        case Kind::InvalidEscape: {
            auto doc = Document{{Paragraph{String{"These Escape sequences are unknown."}, {}},
                                 SourceCodeBlock{escapedLines, highlights, String{}, sl.position.line}}};
            auto expl = Explanation{String("Unkown escape sequence"), doc};
            auto d = Diagnostic{Code{String{"rebuild-lexer"}, 11}, Parts{expl}};
            context.reportDiagnostic(std::move(d));
            break;
        }
        case Kind::InvalidControl: {
            auto doc = Document{{Paragraph{String{"Use of invalid control characters. Use escape sequences."}, {}},
                                 SourceCodeBlock{escapedLines, highlights, String{}, sl.position.line}}};
            auto expl = Explanation{String("Unkown control characters"), doc};
            auto d = Diagnostic{Code{String{"rebuild-lexer"}, 12}, Parts{expl}};
            context.reportDiagnostic(std::move(d));
            break;
        }
        case Kind::InvalidDecimalUnicode: {
            auto doc = Document{{Paragraph{String{"Use of invalid decimal unicode values."}, {}},
                                 SourceCodeBlock{escapedLines, highlights, String{}, sl.position.line}}};
            auto expl = Explanation{String("Invalid decimal unicode"), doc};
            auto d = Diagnostic{Code{String{"rebuild-lexer"}, 13}, Parts{expl}};
            context.reportDiagnostic(std::move(d));
            break;
        }
        case Kind::InvalidHexUnicode: {
            auto doc = Document{{Paragraph{String{"Use of invalid hexadecimal unicode values."}, {}},
                                 SourceCodeBlock{escapedLines, highlights, String{}, sl.position.line}}};
            auto expl = Explanation{String("Invalid hexadecimal unicode"), doc};
            auto d = Diagnostic{Code{String{"rebuild-lexer"}, 14}, Parts{expl}};
            context.reportDiagnostic(std::move(d));
            break;
        }
        } // switch
    }

    auto viewMarkers = ViewMarkers{};
}

template<class Context>
void reportNumberLiteral(
    const nesting::BlockLine& blockLine, const nesting::NumberLiteral& nl, ContextApi<Context>& context) {
    if (nl.isTainted || !nl.value.hasErrors()) return;

    using namespace diagnostic;

    auto tokenLines = extractViewLines(blockLine, nl.input);

    auto reportedKinds = std::bitset<scanner::NumberLiteralError::optionCount()>{};
    for (auto& err : nl.value.errors) {
        auto kind = err.index().value();
        if (reportedKinds[kind]) continue;
        reportedKinds.set(kind);

        auto viewMarkers = ViewMarkers{};
        for (auto& err2 : nl.value.errors)
            if (err2.index() == err.index()) err2.visit([&](auto& v) { viewMarkers.emplace_back(v.input); });

        auto [escapedLines, escapedMarkers] = escapeSourceLine(tokenLines, viewMarkers);

        auto highlights = Highlights{};
        for (auto& m : escapedMarkers) highlights.emplace_back(Marker{m, {}});

        err.visit(
            [&](const scanner::DecodedErrorPosition&) {
                reportDecodeErrorMarkers(nl.position.line, tokenLines, viewMarkers, context);
            },
            [&](const scanner::NumberMissingExponent&) {
                auto doc = Document{{Paragraph{String{"After the exponent sign an actual value is expected."}, {}},
                                     SourceCodeBlock{escapedLines, highlights, String{}, nl.position.line}}};
                auto expl = Explanation{String("Missing exponent value"), doc};
                auto d = Diagnostic{Code{String{"rebuild-lexer"}, 20}, Parts{expl}};
                context.reportDiagnostic(std::move(d));
            },
            [&](const scanner::NumberMissingValue&) {
                auto doc = Document{{Paragraph{String{"After the radix sign an actual value is expected."}, {}},
                                     SourceCodeBlock{escapedLines, highlights, String{}, nl.position.line}}};
                auto expl = Explanation{String("Missing value"), doc};
                auto d = Diagnostic{Code{String{"rebuild-lexer"}, 21}, Parts{expl}};
                context.reportDiagnostic(std::move(d));
            },
            [&](const scanner::NumberMissingBoundary&) {
                auto doc = Document{{Paragraph{String{"The number literal ends with an unknown suffix."}, {}},
                                     SourceCodeBlock{escapedLines, highlights, String{}, nl.position.line}}};
                auto expl = Explanation{String("Missing boundary"), doc};
                auto d = Diagnostic{Code{String{"rebuild-lexer"}, 22}, Parts{expl}};
                context.reportDiagnostic(std::move(d));
            });
    }
}

template<class Context>
void reportOperatorLiteral(
    const nesting::BlockLine& blockLine, const nesting::OperatorLiteral& ol, ContextApi<Context>& context) {
    if (ol.isTainted || !ol.value.hasErrors()) return;

    using namespace diagnostic;

    auto tokenLines = extractViewLines(blockLine, ol.input);

    auto reportedKinds = std::bitset<scanner::OperatorLiteralError::optionCount()>{};
    for (auto& err : ol.value.errors) {
        auto kind = err.index().value();
        if (reportedKinds[kind]) continue;
        reportedKinds.set(kind);

        auto viewMarkers = ViewMarkers{};
        for (auto& err2 : ol.value.errors)
            if (err2.index() == err.index()) err2.visit([&](auto& v) { viewMarkers.emplace_back(v.input); });

        auto [escapedLines, escapedMarkers] = escapeSourceLine(tokenLines, viewMarkers);

        auto highlights = Highlights{};
        for (auto& m : escapedMarkers) highlights.emplace_back(Marker{m, {}});

        err.visit(
            [&](const scanner::DecodedErrorPosition&) {
                reportDecodeErrorMarkers(ol.position.line, tokenLines, viewMarkers, context);
            },
            [&](const scanner::OperatorWrongClose&) {
                auto doc = Document{{Paragraph{String{"The closing sign does not match the opening sign."}, {}},
                                     SourceCodeBlock{escapedLines, highlights, String{}, ol.position.line}}};
                auto expl = Explanation{String("Operator wrong close"), doc};
                auto d = Diagnostic{Code{String{"rebuild-lexer"}, 30}, Parts{expl}};
                context.reportDiagnostic(std::move(d));
            },
            [&](const scanner::OperatorUnexpectedClose&) {
                auto doc = Document{{Paragraph{String{"There was no opening sign before the closing sign."}, {}},
                                     SourceCodeBlock{escapedLines, highlights, String{}, ol.position.line}}};
                auto expl = Explanation{String("Operator unexpected close"), doc};
                auto d = Diagnostic{Code{String{"rebuild-lexer"}, 31}, Parts{expl}};
                context.reportDiagnostic(std::move(d));
            },
            [&](const scanner::OperatorNotClosed&) {
                auto doc = Document{{Paragraph{String{"The operator ends before the closing sign was found."}, {}},
                                     SourceCodeBlock{escapedLines, highlights, String{}, ol.position.line}}};
                auto expl = Explanation{String("Operator not closed"), doc};
                auto d = Diagnostic{Code{String{"rebuild-lexer"}, 32}, Parts{expl}};
                context.reportDiagnostic(std::move(d));
            });
    }
}
} // namespace parser
