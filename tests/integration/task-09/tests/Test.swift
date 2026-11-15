@main struct Tests {
    static func main(){
        precondition(mergeIntervals([Interval(start:5,end:7),Interval(start:1,end:3),Interval(start:3,end:5)]) == [Interval(start:1,end:7)]);precondition(mergeIntervals([]).isEmpty);precondition(mergeIntervals([Interval(start:4,end:2),Interval(start:8,end:9)]) == [Interval(start:8,end:9)])}}
