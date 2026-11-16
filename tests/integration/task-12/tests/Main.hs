import API
import Solution
main :: IO ()
main = do
  if eval (Add (Lit 2) (Mul (Lit 3) (Lit 4))) /= Just 14 then error "precedence" else pure ()
  if eval (Div (Lit 3) (Lit 0)) /= Nothing then error "zero" else pure ()
  if eval (Div (Lit (-7)) (Lit 3)) /= Just (-3) then error "div" else pure ()
  if eval (Add (Lit 1) (Div (Lit 1) (Lit 0))) /= Nothing then error "propagate" else pure ()
