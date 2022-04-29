from itertools import count
import svs
import cv2 as cv
import numpy as np

# check if camera found
num = svs.number_cameras()
print('Number of camera found: ', num)

cam = svs.Camera()
cam.framerate = 5               # Capture 5 image per second
cam.exposure = 40               # Exposure time in milliseconds
cam.continuous_capture = True   # Start image capture

# next() method to grab the first image from the queue
img, meta = cam.next()

# When finished capturing images, stop continuous capture.

cam.continuous_capture = False


# it is important to note that cv2.imwrite expects an 8-bit Numpy array, 
# but the next() method will return a 16-bit Numpy array, so it needs to be converted before saving.

rgb = cv.cvtColor(img, cv.COLOR_BAYER_GR2RGB)
cv.imwrite('capture.png', rgb)


rgb8 = np.right_shift(rgb, 8)
cv.imwrite('capture.jpg', rgb8)
