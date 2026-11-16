module API (Expr(..)) where
data Expr = Lit Integer | Add Expr Expr | Sub Expr Expr | Mul Expr Expr | Div Expr Expr deriving (Eq,Show)