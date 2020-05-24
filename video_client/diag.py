class moving_avg:
    def __init__(self, window_size):
        self.window_size = window_size
        self.val_buf = []

    def add_moving_avg(self, val):
        if len(self.val_buf) >= self.window_size:
            self.val_buf.pop(0)
        self.val_buf.append(val)
        return sum(self.val_buf) / len(self.val_buf)
