import cv2

test = b'\x01\x21'
print(int.from_bytes(test, byteorder = 'big'))
print(test[0])

l1 = [0, 1, 2]
l2 = [4, 5, 6]
l3 = [7, 8, 9]
l4 = [10, 11]
l5 = [12]

c1 = []
c1.insert(1, l2)
c1.insert(0, l1)
c1.insert(2, l3)
c1.insert(4, l5)
c1.insert(3, l4)
print(c1)
print(c1[1])

f1 = []
for x in c1:
    for i in x:
        f1.append(i)

print(f1)

img = cv2.imread('test.jpg', cv2.IMREAD_COLOR)
cv2.imshow('image', img)

cv2.waitKey(0)
cv2.destroyAllWindows()