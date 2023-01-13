class Common:

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
