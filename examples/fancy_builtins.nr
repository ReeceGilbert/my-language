print("=== Fancy builtins ===")

print(abs(-42))
print(floor(3.9))
print(ceil(3.1))
print(min(9, 4, 7, 2))
print(max(9, 4, 7, 2))

seed_random(123)
print(random())
print(randint(10))

start = clock_ms()
x = 0
while x < 1000:
    x = x + 1
end = clock_ms()

print("elapsed ms:")
print(end - start)

profile = {
    "name": "Reece",
    "project": "Nearoh",
    "level": 1
}

print(keys(profile))
print(values(profile))