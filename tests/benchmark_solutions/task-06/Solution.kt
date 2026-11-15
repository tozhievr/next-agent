fun aggregate(events: List<Event>): List<UserTotal> {val sums=linkedMapOf<String,Long>();for(e in events)sums[e.user]=(sums[e.user]?:0L)+e.amount;return sums.map{UserTotal(it.key,it.value)}}
