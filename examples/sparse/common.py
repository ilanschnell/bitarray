class Common:

    def __repr__(self):
        return "SparseBitarray('%s')" % (''.join(str(v) for v in self))

    def pop(self, i = -1):
        if i < 0:
            i += len(self)
        res = self[i]
        del self[i]
        return res

    def remove(self, value):
        i = self.find(value)
        if i < 0:
            raise ValueError
        del self[i]

    def sort(self, reverse=False):
        if reverse:
            c1 = self.count(1)
            self[:c1] = 1
            self[c1:] = 0
        else:
            c0 = self.count(0)
            self[:c0] = 0
            self[c0:] = 1

    def _get_start_stop(self, key):
        if key.step not in (1, None):
            raise ValueError("only step = 1 allowed, got %r" % key)
        start = key.start
        if start is None:
            start = 0
        stop = key.stop
        if stop is None:
            stop = len(self)
        return start, stop

    def _adjust_index(self, i):
        n = len(self)
        if i < 0:
            i += n
            if i < 0:
                i = 0
        elif i > n:
            i = n
        return i
