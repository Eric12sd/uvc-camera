# Linux simple camera

#### Function
Drive an external USB camera based on V4L2 architecture to achieve the functions of taking photos and viewing albums


#### Involving technology

This project mainly involves the application of the V4L2 framework in Linux, mainly to obtain camera data. The application of the JPEG open source library is used to convert the obtained image data from JPEG to RGB format for display on LCD. The TSLib library implements touch operations on the touch screen, multi-threaded operations, and bidirectional linked list storage for photos.

#### Note

1. Replace the UVC driver
2. It is necessary to transplant the JPEG library and the corresponding cross compilation chain of the board by oneself
3. The background image corresponds to background1.jpg and background2.jpg in the code




