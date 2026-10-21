// src/tools/calculator_eval.cpp — calc_evaluate (spec §4.6).
//
// The two-stack algorithm: one stack for numbers, one for operators and '('.
// Loops only (no recursion), so very long or deeply nested inputs are fine.

#include "tools/calculator.h"

#include <stack>

namespace {

using Kind = CalcToken::Kind;

// * and / bind tighter than + and -.
int precedence(Kind op) {
    return (op == Kind::Times || op == Kind::Divide) ? 2 : 1;
}

bool is_operator(Kind k) {
    return k == Kind::Plus || k == Kind::Minus || k == Kind::Times || k == Kind::Divide;
}

// Pops one operator and two numbers, then pushes the result.
bool apply_top(std::stack<double>& values, std::stack<Kind>& ops, std::string* error) {
    if (ops.empty() || values.size() < 2) {
        *error = "malformed expression";
        return false;
    }
    Kind op = ops.top();
    ops.pop();

    // The first pop is the RIGHT operand, the second is the LEFT one.
    double right = values.top();
    values.pop();
    double left = values.top();
    values.pop();

    double result = 0.0;
    switch (op) {
        case Kind::Plus:   result = left + right; break;
        case Kind::Minus:  result = left - right; break;
        case Kind::Times:  result = left * right; break;
        case Kind::Divide:
            if (right == 0.0) {
                *error = "division by zero";
                return false;
            }
            result = left / right;
            break;
        default:
            *error = "malformed expression";
            return false;
    }
    values.push(result);
    return true;
}

}  // namespace

bool calc_evaluate(const std::vector<CalcToken>& tokens, double* out, std::string* error) {
    std::stack<double> values;
    std::stack<Kind> ops;

    for (const CalcToken& tok : tokens) {
        if (tok.kind == Kind::Number) {
            values.push(tok.value);
        } else if (tok.kind == Kind::LParen) {
            ops.push(tok.kind);
        } else if (tok.kind == Kind::RParen) {
            // Finish everything inside the parentheses, then drop the '('.
            while (!ops.empty() && ops.top() != Kind::LParen) {
                if (!apply_top(values, ops, error)) return false;
            }
            if (ops.empty()) {
                *error = "unbalanced parentheses";
                return false;
            }
            ops.pop();
        } else {
            // Apply waiting operators of equal or higher precedence first (>= keeps it left to right).
            while (!ops.empty() && is_operator(ops.top()) &&
                   precedence(ops.top()) >= precedence(tok.kind)) {
                if (!apply_top(values, ops, error)) return false;
            }
            ops.push(tok.kind);
        }
    }

    // Use up whatever operators are left.
    while (!ops.empty()) {
        if (ops.top() == Kind::LParen) {
            *error = "unbalanced parentheses";
            return false;
        }
        if (!apply_top(values, ops, error)) return false;
    }

    // A good expression leaves exactly one number.
    if (values.size() != 1) {
        *error = "malformed expression";
        return false;
    }
    *out = values.top();
    return true;
}
