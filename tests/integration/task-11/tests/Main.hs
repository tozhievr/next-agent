import Solution
main :: IO ()
main = do
  if rotate 2 [1,2,3,4] /= [3,4,1,2] then error "left" else pure ()
  if rotate (-1) [1,2,3] /= [3,1,2] then error "right" else pure ()
  if rotate 10 ([] :: [Int]) /= [] then error "empty" else pure ()
  if rotate 3 [1,2,3] /= [1,2,3] then error "full" else pure ()
