class Point():
    def __init__(self, x, y):
        self.x = x
        self.y = y

points = [Point(1, 2), Point(3, 4), Point(5, 6)]

for p in points:
    print(p.x)
    print(p.y)