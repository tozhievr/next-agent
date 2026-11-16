module Solution (eval) where
import API
eval :: Expr -> Maybe Integer
eval (Lit n) = Just n
eval (Add a b) = (+) <$> eval a <*> eval b
eval (Sub a b) = (-) <$> eval a <*> eval b
eval (Mul a b) = (*) <$> eval a <*> eval b
eval (Div a b) = do
 x <- eval a
 y <- eval b
 if y == 0 then Nothing else Just (x `div` y)
