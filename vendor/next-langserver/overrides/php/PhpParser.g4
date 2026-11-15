/*
PHP grammar.
The MIT License (MIT).
Copyright (c) 2015-2020, Ivan Kochurkin (kvanttt@gmail.com), Positive Technologies.
Copyright (c) 2019-2020, Student Main for php7, php8 support.

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.
*/

// $antlr-format alignTrailingComments true, columnLimit 150, minEmptyLines 1, maxEmptyLinesToKeep 1, reflowComments false, useTab false
// $antlr-format allowShortRulesOnASingleLine false, allowShortBlocksOnASingleLine true, alignSemicolons hanging, alignColons hanging

parser grammar PhpParser;

options {
    tokenVocab = PhpLexer;
}

// HTML
// Also see here: https://github.com/antlr/grammars-v4/tree/master/html

htmlDocument
    : Shebang? (inlineHtml | phpBlock)* EOF
    ;

inlineHtml
    : htmlElement+
    | scriptText
    ;

// TODO: split into html, css and xml elements
htmlElement
    : HtmlDtd
    | HtmlClose
    | HtmlStyleOpen
    | HtmlOpen
    | HtmlName
    | HtmlSlashClose
    | HtmlSlash
    | HtmlText
    | HtmlEquals
    | HtmlStartQuoteString
    | HtmlEndQuoteString
    | HtmlStartDoubleQuoteString
    | HtmlEndDoubleQuoteString
    | HtmlHex
    | HtmlDecimal
    | HtmlQuoteString
    | HtmlDoubleQuoteString
    | StyleBody
    | HtmlScriptOpen
    | HtmlScriptClose
    | XmlStart XmlText* XmlClose
    ;

// Script
// Parse JavaScript with https://github.com/antlr/grammars-v4/tree/master/javascript if necessary.

scriptText
    : ScriptText+
    ;

// PHP

phpBlock
    : importStatement* topStatement+
    ;

importStatement
    : Import Namespace namespaceNameList SemiColon
    ;

topStatement
    : statement
    | useDeclaration
    | namespaceDeclaration
    | functionDeclaration
    | classDeclaration
    | globalConstantDeclaration
    | enumDeclaration
    ;

useDeclaration
    : Use (Function_ | Const)? useDeclarationContentList SemiColon
    ;

useDeclarationContentList
    : NamespaceSeparator? useDeclarationContent (Comma NamespaceSeparator? useDeclarationContent)*
    ;

useDeclarationContent
    : namespaceNameList
    ;

namespaceDeclaration
    : Namespace (
        namespaceNameList? OpenCurlyBracket namespaceStatement* CloseCurlyBracket
        | namespaceNameList SemiColon
    )
    ;

namespaceStatement
    : statement
    | useDeclaration
    | functionDeclaration
    | classDeclaration
    | globalConstantDeclaration
    ;

functionDeclaration
    : attributes? Function_ Ampersand? identifier typeParameterListInBrackets? OpenRoundBracket formalParameterList CloseRoundBracket (
        Colon QuestionMark? typeHint
    )? blockStatement
    ;

classDeclaration
    : attributes? Private? modifier? Partial? (
        classEntryType identifier typeParameterListInBrackets? (Extends qualifiedStaticTypeRef)? (
            Implements interfaceList
        )?
        | Interface identifier typeParameterListInBrackets? (Extends interfaceList)?
    ) OpenCurlyBracket classStatement* CloseCurlyBracket
    ;

classEntryType
    : Class
    | Trait
    ;

interfaceList
    : qualifiedStaticTypeRef (Comma qualifiedStaticTypeRef)*
    ;

typeParameterListInBrackets
    : Lgeneric typeParameterList Rgeneric
    | Lgeneric typeParameterWithDefaultsList Rgeneric
    | Lgeneric typeParameterList Comma typeParameterWithDefaultsList Rgeneric
    ;

typeParameterList
    : typeParameterDecl (Comma typeParameterDecl)*
    ;

typeParameterWithDefaultsList
    : typeParameterWithDefaultDecl (Comma typeParameterWithDefaultDecl)*
    ;

typeParameterDecl
    : attributes? identifier
    ;

typeParameterWithDefaultDecl
    : attributes? identifier Eq (qualifiedStaticTypeRef | primitiveType)
    ;

genericDynamicArgs
    : Lgeneric typeRef (Comma typeRef)* Rgeneric
    ;

attributes
    : attributeGroup+
    ;

attributeGroup
    : AttributeStart (identifier Colon)? attribute (Comma attribute)* CloseSquareBracket
    ;

attribute
    : qualifiedNamespaceName arguments?
    ;

innerStatementList
    : innerStatement*
    ;

innerStatement
    : statement
    | functionDeclaration
    | classDeclaration
    ;

// Statements

statement
    : identifier Colon
    | blockStatement
    | ifStatement
    | whileStatement
    | doWhileStatement
    | forStatement
    | switchStatement
    | breakStatement
    | continueStatement
    | returnStatement
    | yieldExpression SemiColon
    | globalStatement
    | staticVariableStatement
    | echoStatement
    | expressionStatement
    | unsetStatement
    | foreachStatement
    | tryCatchFinally
    | throwStatement
    | gotoStatement
    | declareStatement
    | emptyStatement_
    | inlineHtmlStatement
    ;

emptyStatement_
    : SemiColon
    ;

blockStatement
    : OpenCurlyBracket innerStatementList CloseCurlyBracket
    ;

ifStatement
    : If parentheses statement elseIfStatement* elseStatement?
    | If parentheses Colon innerStatementList elseIfColonStatement* elseColonStatement? EndIf SemiColon
    ;

elseIfStatement
    : ElseIf parentheses statement
    ;

elseIfColonStatement
    : ElseIf parentheses Colon innerStatementList
    ;

elseStatement
    : Else statement
    ;

elseColonStatement
    : Else Colon innerStatementList
    ;

whileStatement
    : While parentheses (statement | Colon innerStatementList EndWhile SemiColon)
    ;

doWhileStatement
    : Do statement While parentheses SemiColon
    ;

forStatement
    : For OpenRoundBracket forInit? SemiColon expressionList? SemiColon forUpdate? CloseRoundBracket (
        statement
        | Colon innerStatementList EndFor SemiColon
    )
    ;

forInit
    : expressionList
    ;

forUpdate
    : expressionList
    ;

switchStatement
    : Switch parentheses (
        OpenCurlyBracket SemiColon? switchBlock* CloseCurlyBracket
        | Colon SemiColon? switchBlock* EndSwitch SemiColon
    )
    ;

switchBlock
    : ((Case expression | Default) (Colon | SemiColon))+ innerStatementList
    ;

breakStatement
    : Break expression? SemiColon
    ;

continueStatement
    : Continue expression? SemiColon
    ;

returnStatement
    : Return expression? SemiColon
    ;

expressionStatement
    : expression SemiColon
    ;

unsetStatement
    : Unset OpenRoundBracket chainList CloseRoundBracket SemiColon
    ;

foreachStatement
    : Foreach (
        OpenRoundBracket expression As arrayDestructuring CloseRoundBracket
        | OpenRoundBracket chain As Ampersand? assignable (DoubleArrow Ampersand? chain)? CloseRoundBracket
        | OpenRoundBracket expression As assignable (DoubleArrow Ampersand? chain)? CloseRoundBracket
        | OpenRoundBracket chain As List OpenRoundBracket assignmentList CloseRoundBracket CloseRoundBracket
    ) (statement | Colon innerStatementList EndForeach SemiColon)
    ;

tryCatchFinally
    : Try blockStatement (catchClause+ finallyStatement? | catchClause* finallyStatement)
    ;

catchClause
    : Catch OpenRoundBracket qualifiedStaticTypeRef (Pipe qualifiedStaticTypeRef)* VarName? CloseRoundBracket blockStatement
    ;

finallyStatement
    : Finally blockStatement
    ;

throwStatement
    : Throw expression SemiColon
    ;

gotoStatement
    : Goto identifier SemiColon
    ;

declareStatement
    : Declare OpenRoundBracket declareList CloseRoundBracket (statement | Colon innerStatementList EndDeclare SemiColon)
    ;

inlineHtmlStatement
    : inlineHtml+
    ;

declareList
    : directive (Comma directive)*
    ;

directive
    : Ticks Eq (numericConstant | Real)
    | Encoding Eq SingleQuoteString
    | StrictTypes Eq numericConstant
    ;

formalParameterList
    : formalParameter? (Comma formalParameter)* Comma?
    ;

formalParameter
    : attributes? memberModifier* QuestionMark? typeHint? Ampersand? Ellipsis? variableInitializer
    ;

typeHint
    : qualifiedStaticTypeRef
    | Callable
    | primitiveType
    | typeHint Pipe typeHint
    ;

globalStatement
    : Global globalVar (Comma globalVar)* SemiColon
    ;

globalVar
    : VarName
    | Dollar chain
    | Dollar OpenCurlyBracket expression CloseCurlyBracket
    ;

echoStatement
    : Echo expressionList SemiColon
    ;

staticVariableStatement
    : Static variableInitializer (Comma variableInitializer)* SemiColon
    ;

classStatement
    : attributes? (
        propertyModifiers typeHint? variableInitializer (Comma variableInitializer)* SemiColon
        | memberModifiers? (
            Const typeHint? identifierInitializer (Comma identifierInitializer)* SemiColon
            | Function_ Ampersand? identifier typeParameterListInBrackets? OpenRoundBracket formalParameterList CloseRoundBracket (
                baseCtorCall
                | returnTypeDecl
            )? methodBody
        )
    )
    | Use qualifiedNamespaceNameList traitAdaptations
    ;

traitAdaptations
    : SemiColon
    | OpenCurlyBracket traitAdaptationStatement* CloseCurlyBracket
    ;

traitAdaptationStatement
    : traitPrecedence
    | traitAlias
    ;

traitPrecedence
    : qualifiedNamespaceName DoubleColon identifier InsteadOf qualifiedNamespaceNameList SemiColon
    ;

traitAlias
    : traitMethodReference As (memberModifier | memberModifier? identifier) SemiColon
    ;

traitMethodReference
    : (qualifiedNamespaceName DoubleColon)? identifier
    ;

baseCtorCall
    : Colon identifier arguments?
    ;

returnTypeDecl
    : Colon QuestionMark? typeHint
    ;

methodBody
    : SemiColon
    | blockStatement
    ;

propertyModifiers
    : memberModifiers
    | Var
    ;

memberModifiers
    : memberModifier+
    ;

variableInitializer
    : VarName (Eq constantInitializer)?
    ;

identifierInitializer
    : identifier Eq constantInitializer
    ;

globalConstantDeclaration
    : attributes? Const identifierInitializer (Comma identifierInitializer)* SemiColon
    ;

enumDeclaration
    : Enum_ identifier (Colon (IntType | StringType))? (Implements interfaceList)? OpenCurlyBracket enumItem* CloseCurlyBracket
    ;

enumItem
    : Case identifier (Eq expression)? SemiColon
    | memberModifiers? functionDeclaration
    | Use qualifiedNamespaceNameList traitAdaptations
    ;

expressionList
    : expression (Comma expression)*
    ;

parentheses
    : OpenRoundBracket (expression | yieldExpression) CloseRoundBracket
    ;

// Expressions
// Grouped by priorities: http://php.net/manual/en/language.operators.precedence.php
expression
    : Clone expression                                            # CloneExpression
    | newExpr                                                     # NewExpression
    | stringConstant OpenSquareBracket expression CloseSquareBracket                           # IndexerExpression
    | OpenRoundBracket castOperation CloseRoundBracket expression                            # CastExpression
    | (Tilde | SuppressWarnings) expression                                      # UnaryOperatorExpression
    | (Bang | Plus | Minus) expression                                # UnaryOperatorExpression
    | (Inc | Dec) chain                                         # PrefixIncDecExpression
    | chain (Inc | Dec)                                         # PostfixIncDecExpression
    | Print expression                                            # PrintExpression
    | arrayCreation                                               # ArrayCreationExpression
    | chain                                                       # ChainExpression
    | constant                                                    # ScalarExpression
    | string                                                      # ScalarExpression
    | Label                                                       # ScalarExpression
    | BackQuoteString                                             # BackQuoteStringExpression
    | parentheses                                                 # ParenthesisExpression
    | Yield                                                       # SpecialWordExpression
    | List OpenRoundBracket assignmentList CloseRoundBracket Eq expression                   # SpecialWordExpression
    | IsSet OpenRoundBracket chainList CloseRoundBracket                                     # SpecialWordExpression
    | Empty OpenRoundBracket chain CloseRoundBracket                                         # SpecialWordExpression
    | Eval OpenRoundBracket expression CloseRoundBracket                                     # SpecialWordExpression
    | Exit ( OpenRoundBracket CloseRoundBracket | parentheses)?                              # SpecialWordExpression
    | lambdaFunctionExpr                                          # LambdaFunctionExpression
    | matchExpr                                                   # MatchExpression
    | <assoc = right> expression op = Pow expression             # ExponentiationExpression
    | expression InstanceOf typeRef                               # InstanceOfExpression
    | expression op = (Asterisk | Divide | Percent) expression             # MultiplicativeExpression
    | expression op = (Plus | Minus | Dot) expression                # AdditiveExpression
    | expression op = (ShiftLeft | ShiftRight) expression                    # ShiftExpression
    | expression op = (Less | IsSmallerOrEqual | Greater | IsGreaterOrEqual) expression   # RelationalExpression
    | expression op = (IsIdentical | IsNoidentical | IsEqual | IsNotEq) expression # EqualityExpression
    | expression op = Ampersand expression                              # BitwiseAndExpression
    | expression op = Caret expression                              # BitwiseExcOrExpression
    | expression op = Pipe expression                              # BitwiseIncOrExpression
    | expression op = BooleanAnd expression                             # LogicalAndExpression
    | expression op = BooleanOr expression                             # LogicalIncOrExpression
    | expression op = QuestionMark expression? Colon expression     # ConditionalExpression
    | expression op = NullCoalescing expression                             # CoalesceExpression
    | expression op = Spaceship expression                            # SpaceshipExpression
    | (Include | IncludeOnce) expression                          # IncludeExpression
    | (Require | RequireOnce) expression                          # RequireExpression
    | Throw expression                                            # SpecialWordExpression
    | arrayDestructuring Eq expression                            # ArrayDestructExpression
    | assignable assignmentOperator attributes? expression        # AssignmentExpression
    | assignable Eq attributes? Ampersand (chain | newExpr)             # AssignmentExpression
    | expression op = LogicalAnd expression                       # LogicalAndExpression
    | expression op = LogicalXor expression                       # LogicalExcOrExpression
    | expression op = LogicalOr expression                        # LogicalIncOrExpression
    ;

assignable
    : chain
    | arrayCreation
    ;

arrayCreation
    : (Array OpenRoundBracket arrayItemList? CloseRoundBracket | OpenSquareBracket arrayItemList? CloseSquareBracket) (OpenSquareBracket expression CloseSquareBracket)?
    ;

arrayDestructuring
    : OpenSquareBracket Comma* indexedDestructItem (Comma+ indexedDestructItem)* Comma* CloseSquareBracket
    | OpenSquareBracket keyedDestructItem (Comma+ keyedDestructItem)* Comma? CloseSquareBracket
    ;

indexedDestructItem
    : Ampersand? chain
    ;

keyedDestructItem
    : (expression DoubleArrow)? Ampersand? chain
    ;

lambdaFunctionExpr
    : Static? Function_ Ampersand? OpenRoundBracket formalParameterList CloseRoundBracket lambdaFunctionUseVars? (Colon typeHint)? blockStatement
    | LambdaFn OpenRoundBracket formalParameterList CloseRoundBracket DoubleArrow expression
    ;

matchExpr
    : Match_ OpenRoundBracket expression CloseRoundBracket OpenCurlyBracket matchItem (Comma matchItem)* Comma? CloseCurlyBracket
    ;

matchItem
    : expression (Comma expression)* DoubleArrow expression
    ;

newExpr
    : New typeRef arguments?
    ;

assignmentOperator
    : Eq
    | PlusEqual
    | MinusEqual
    | MulEqual
    | PowEqual
    | DivEqual
    | Concaequal
    | ModEqual
    | AndEqual
    | OrEqual
    | XorEqual
    | ShiftLeftEqual
    | ShiftRightEqual
    | NullCoalescingEqual
    ;

yieldExpression
    : Yield (expression (DoubleArrow expression)? | From expression)
    ;

arrayItemList
    : arrayItem (Comma arrayItem)* Comma?
    ;

arrayItem
    : expression (DoubleArrow expression)?
    | (expression DoubleArrow)? Ampersand chain
    ;

lambdaFunctionUseVars
    : Use OpenRoundBracket lambdaFunctionUseVar (Comma lambdaFunctionUseVar)* CloseRoundBracket
    ;

lambdaFunctionUseVar
    : Ampersand? VarName
    ;

qualifiedStaticTypeRef
    : qualifiedNamespaceName genericDynamicArgs?
    | Static
    ;

typeRef
    : (qualifiedNamespaceName | indirectTypeRef) genericDynamicArgs?
    | primitiveType
    | Static
    | anonymousClass
    ;

anonymousClass
    : attributes? Private? modifier? Partial? (
        classEntryType typeParameterListInBrackets? (Extends qualifiedStaticTypeRef)? (
            Implements interfaceList
        )?
        | Interface identifier typeParameterListInBrackets? (Extends interfaceList)?
    ) OpenCurlyBracket classStatement* CloseCurlyBracket
    ;

indirectTypeRef
    : chainBase (ObjectOperator keyedFieldName)*
    ;

qualifiedNamespaceName
    : Namespace? NamespaceSeparator? namespaceNameList
    ;

namespaceNameList
    : identifier
    | identifier (NamespaceSeparator identifier)* (NamespaceSeparator namespaceNameTail)?
    ;

namespaceNameTail
    : identifier (As identifier)?
    | OpenCurlyBracket namespaceNameTail (Comma namespaceNameTail)* Comma? CloseCurlyBracket
    ;

qualifiedNamespaceNameList
    : qualifiedNamespaceName (Comma qualifiedNamespaceName)*
    ;

arguments
    : OpenRoundBracket (actualArgument (Comma actualArgument)* | yieldExpression)? Comma? CloseRoundBracket
    ;

actualArgument
    : argumentName? Ellipsis? expression
    | Ampersand chain
    ;

argumentName
    : identifier Colon
    ;

constantInitializer
    : constant
    | string
    | Array OpenRoundBracket (arrayItemList Comma?)? CloseRoundBracket
    | OpenSquareBracket (arrayItemList Comma?)? CloseSquareBracket
    | (Plus | Minus) constantInitializer
    | (string | constant) (Dot (string | constant))*
    ;

constant
    : Null
    | literalConstant
    | magicConstant
    | classConstant
    | qualifiedNamespaceName
    ;

literalConstant
    : Real
    | BooleanConstant
    | numericConstant
    | stringConstant
    ;

numericConstant
    : Octal
    | Decimal
    | Hex
    | Binary
    ;

classConstant
    : (Class | Parent_) DoubleColon (identifier | Constructor | Get | Set)
    | (qualifiedStaticTypeRef | keyedVariable | string) DoubleColon (
        identifier
        | keyedVariable
    ) // 'foo'::$bar works in php7
    ;

stringConstant
    : Label
    ;

string
    : StartHereDoc HereDocText+
    | StartNowDoc HereDocText+
    | SingleQuoteString
    | DoubleQuote interpolatedStringPart* DoubleQuote
    ;

interpolatedStringPart
    : StringPart
    | UnicodeEscape
    | chain
    ;

chainList
    : chain (Comma chain)*
    ;

chain
    : chainOrigin memberAccess*
    //| arrayCreation // [$a,$b]=$c
    ;

chainOrigin
    : chainBase
    | functionCall
    | OpenRoundBracket newExpr CloseRoundBracket
    ;

memberAccess
    : ObjectOperator keyedFieldName actualArguments?
    ;

functionCall
    : functionCallName actualArguments
    ;

functionCallName
    : qualifiedNamespaceName
    | classConstant
    | chainBase
    | parentheses
    ;

actualArguments
    : genericDynamicArgs? arguments+ squareCurlyExpression*
    ;

chainBase
    : keyedVariable (DoubleColon keyedVariable)?
    | qualifiedStaticTypeRef DoubleColon keyedVariable
    ;

keyedFieldName
    : keyedSimpleFieldName
    | keyedVariable
    ;

keyedSimpleFieldName
    : (identifier | OpenCurlyBracket expression CloseCurlyBracket) squareCurlyExpression*
    ;

keyedVariable
    : Dollar* (VarName | Dollar OpenCurlyBracket expression CloseCurlyBracket) squareCurlyExpression*
    ;

squareCurlyExpression
    : OpenSquareBracket expression? CloseSquareBracket
    | OpenCurlyBracket expression CloseCurlyBracket
    ;

assignmentList
    : assignmentListElement? (Comma assignmentListElement?)*
    ;

assignmentListElement
    : chain
    | List OpenRoundBracket assignmentList CloseRoundBracket
    | arrayItem
    ;

modifier
    : Abstract
    | Final
    ;

identifier
    : Label
    | Abstract
    | Array
    | As
    | BinaryCast
    | BoolType
    | BooleanConstant
    | Break
    | Callable
    | Case
    | Catch
    | Class
    | Clone
    | Const
    | Continue
    | Declare
    | Default
    | Do
    | DoubleCast
    | DoubleType
    | Echo
    | Else
    | ElseIf
    | Empty
    | EndDeclare
    | EndFor
    | EndForeach
    | EndIf
    | EndSwitch
    | EndWhile
    | Eval
    | Exit
    | Extends
    | Final
    | Finally
    | FloatCast
    | For
    | Foreach
    | Function_
    | Global
    | Goto
    | If
    | Implements
    | Import
    | Include
    | IncludeOnce
    | InstanceOf
    | InsteadOf
    | Int16Cast
    | Int64Type
    | Int8Cast
    | Interface
    | IntType
    | IsSet
    | LambdaFn
    | List
    | LogicalAnd
    | LogicalOr
    | LogicalXor
    | Namespace
    | New
    | Null
    | ObjectType
    | Parent_
    | Partial
    | Print
    | Private
    | Protected
    | Public
    | Readonly
    | Require
    | RequireOnce
    | Resource
    | Return
    | Static
    | StringType
    | Switch
    | Throw
    | Trait
    | Try
    | Typeof
    | UintCast
    | UnicodeCast
    | Unset
    | Use
    | Var
    | While
    | Yield
    | From
    | Enum_
    | Match_
    | Ticks
    | Encoding
    | StrictTypes
    | Get
    | Set
    | Call
    | CallStatic
    | Constructor
    | Destruct
    | Wakeup
    | Sleep
    | Autoload
    | IsSet__
    | Unset__
    | ToString__
    | Invoke
    | SetState
    | Clone__
    | DebugInfo
    | Namespace__
    | Class__
    | Traic__
    | Function__
    | Method__
    | Line__
    | File__
    | Dir__
    ;

memberModifier
    : Public
    | Protected
    | Private
    | Static
    | Abstract
    | Final
    | Readonly
    ;

magicConstant
    : Namespace__
    | Class__
    | Traic__
    | Function__
    | Method__
    | Line__
    | File__
    | Dir__
    ;

primitiveType
    : BoolType
    | IntType
    | Int64Type
    | DoubleType
    | StringType
    | Resource
    | ObjectType
    | Array
    ;

castOperation
    : BoolType
    | Int8Cast
    | Int16Cast
    | IntType
    | Int64Type
    | UintCast
    | DoubleCast
    | DoubleType
    | FloatCast
    | StringType
    | BinaryCast
    | UnicodeCast
    | Array
    | ObjectType
    | Resource
    | Unset
    ;