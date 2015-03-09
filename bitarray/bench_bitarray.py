import timeit

def bench_sequence():
    print('Benchmarking sequence methods')
    baseline = []
    for name, setup in [('list', 's = list(range(20));' +
                                 's1 = [1];' +
                                 's2 = list(range(1000000))'),
                        ('bitarray', 'from bitarray import bitarray;' +
                                     's = bitarray([0, 1]) * 10;' +
                                     's1 = bitarray([1]);' +
                                     's2 = bitarray(1000000)')]:
        print('=== Testing ' + name)
        for i, op in enumerate(['len(s)', '1 in s',
                                's[0]', 's[0] = 1', 'del s2[-1]',
                                's[1:-1]', 's[-2:-1] = s1', 'del s2[-1:]',
                                's + s', 's * 2', 's += s1']):
            t = min(timeit.repeat(op, setup))
            if i < len(baseline):
                b = t / baseline[i]
            else:
                b = ''
                baseline.append(t)
            print('%-24s %.8f\t%s' % (op + ' took:', t, b))
        print('')

def run():
    bench_sequence()

if __name__ == '__main__':
    run()
