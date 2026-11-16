module Solution (rotate) where
rotate :: Int -> [a] -> [a]
rotate _ [] = []
rotate n xs = let k = fromInteger (toInteger n `mod` toInteger (length xs)) in drop k xs ++ take k xs
