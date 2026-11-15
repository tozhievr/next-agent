import Solution
main :: IO ()
main = do
  let edges=[('a','b',10),('a','c',2),('c','b',1),('b','d',4),('c','d',20)]
  if shortestPaths 'a' edges /= [('a',0),('b',3),('c',2),('d',7)] then error "paths" else pure ()
  if shortestPaths 'z' edges /= [('z',0)] then error "isolated" else pure ()
  if shortestPaths (1::Int) [(1,2,5),(1,2,2)] /= [(1,0),(2,2)] then error "parallel" else pure ()
