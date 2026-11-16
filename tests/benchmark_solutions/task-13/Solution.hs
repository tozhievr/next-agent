module Solution (shortestPaths) where
import qualified Data.Map.Strict as M
import qualified Data.Set as S
shortestPaths :: Ord a => a -> [(a,a,Int)] -> [(a,Int)]
shortestPaths source edges = M.toAscList (go (S.singleton (0,source)) (M.singleton source 0)) where
 adjacency = M.fromListWith (++) [(a,[(b,w)]) | (a,b,w) <- edges]
 go queue distances = case S.minView queue of
  Nothing -> distances
  Just ((d,v),rest) | M.lookup v distances /= Just d -> go rest distances
  Just ((d,v),rest) -> let
   relax (q,ds) (n,w) = let candidate=d+w in if w < 0 || d > maxBound-w || maybe False (<=candidate) (M.lookup n ds) then (q,ds) else (S.insert (candidate,n) q,M.insert n candidate ds)
   (next,ds) = foldl relax (rest,distances) (M.findWithDefault [] v adjacency)
   in go next ds
