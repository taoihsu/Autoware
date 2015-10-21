#include "LkTracker.hpp"


LkTracker::LkTracker()
{
	max_point_count_ 		= 500;
	criteria_max_iteration_	= 20;
	criteria_epsilon_		= 0.03;
	corner_window_size_		= 31;
	corner_subwindow_size_	= 10;
	term_criteria_ 			= cv::TermCriteria(	CV_TERMCRIT_ITER|CV_TERMCRIT_EPS,	//type
										criteria_max_iteration_, 					//max iteration count
										criteria_epsilon_							//epsilon
										);
	sub_pixel_window_size_ 	= cv::Size(corner_subwindow_size_, corner_subwindow_size_);
	window_size_ 			= cv::Size(corner_window_size_, corner_window_size_);

	frame_count_			= 0;

	current_centroid_x_		= 0;
	current_centroid_y_		= 0;
	previous_centroid_x_	= 0;
	previous_centroid_y_	= 0;
}

void LkTracker::ArrowedLine(cv::Mat& in_image, cv::Point in_point1, cv::Point in_point2, const cv::Scalar& in_color,
				int in_thickness, int in_line_type, int in_shift, double in_tip_length)
{
	const double tipSize = cv::norm(in_point1-in_point2) * in_tip_length; // Factor to normalize the size of the tip depending on the length of the arrow
	cv::line(in_image, in_point1, in_point2, in_color, in_thickness, in_line_type, in_shift);

	const double angle = atan2( (double) in_point1.y - in_point2.y, (double) in_point1.x - in_point2.x );
	cv::Point p(cvRound(in_point2.x + tipSize * cos(angle + CV_PI / 4)),
	cvRound(in_point2.y + tipSize * sin(angle + CV_PI / 4)));

	cv::line(in_image, p, in_point2, in_color, in_thickness, in_line_type, in_shift);

	p.x = cvRound(in_point2.x + tipSize * cos(angle - CV_PI / 4));
	p.y = cvRound(in_point2.y + tipSize * sin(angle - CV_PI / 4));

	cv::line(in_image, p, in_point2, in_color, in_thickness, in_line_type, in_shift);
}

void OrbFeatures(cv::Mat in_image)
{
	cv::OrbFeatureDetector orb(500);
	std::vector< cv::KeyPoint > keypoints;
	orb.detect(in_image, keypoints);

	cv::OrbDescriptorExtractor extractor;
	cv::Mat descriptors;
	cv::Mat training_descriptors(1, extractor.descriptorSize(), extractor.descriptorType());
	extractor.compute(in_image, keypoints, descriptors);
	training_descriptors.push_back(descriptors);

	cv::BOWKMeansTrainer bow_trainer(2);
	bow_trainer.add(descriptors);

	cv::Mat vocabulary = bow_trainer.cluster();
}

cv::Mat LkTracker::Track(cv::Mat in_image, std::vector<cv::Rect> in_detections, bool in_update)
{
	cv::Mat gray_image;
	cv::cvtColor(in_image, in_image, cv::COLOR_RGB2BGR);
	cv::cvtColor(in_image, gray_image, cv::COLOR_BGR2GRAY);
	cv::Mat mask(gray_image.size(), CV_8UC1);
	cv::TickMeter timer;

	timer.start();

	if (in_update && in_detections.size() > 0)
	{
		//MATCH
		matched_detection_ = in_detections[0];
		if (matched_detection_.x < 0) matched_detection_.x = 0;
		if (matched_detection_.y < 0) matched_detection_.y = 0;
		if (matched_detection_.x + matched_detection_.width > in_image.cols) matched_detection_.width = in_image.cols - matched_detection_.x;
		if (matched_detection_.y + matched_detection_.height > in_image.rows) matched_detection_.height = in_image.rows - matched_detection_.y;

		mask.setTo(cv::Scalar::all(0));
		mask(matched_detection_) = 1;							//fill with ones only the ROI
	}
	int sum_x = 0;
	int sum_y = 0;


	if ( ( in_update || prev_image_.empty() ) &&
		 ( matched_detection_.width>0 && matched_detection_.height >0 )
		)																//add as new object
	{
		cv::goodFeaturesToTrack(gray_image,			//input to extract corners
								current_points_,	//out array with corners in the image
								max_point_count_,	//maximum number of corner points to obtain
								0.01,				//quality level
								10,					//minimum distance between corner points
								mask,//mask ROI
								3,					//block size
								true,				//true to use harris corner detector, otherwise use tomasi
								0.04);				//harris detector free parameter
		/*cv::cornerSubPix(gray_image,
					current_points_,
					sub_pixel_window_size_,
					cv::Size(-1,-1),
					term_criteria_);*/
		//frame_count_ = 0;
		current_centroid_x_ = 0;
		current_centroid_y_ = 0;
		//current_points_.push_back(cv::Point(matched_detection_.x, matched_detection_.y));

		for (std::size_t i = 0; i < current_points_.size(); i++)
		{
			cv::circle(in_image, current_points_[i], 3 , cv::Scalar(0,255,0), 2);
			current_centroid_x_+= current_points_[i].x;
			current_centroid_y_+= current_points_[i].y;
		}
		std::cout << "CENTROID" << current_centroid_x_ <<","<< current_centroid_y_<< std::endl << std::endl;

	}
	else if ( !prev_points_.empty() )//try to match current object
	{
		std::vector<uchar> status;
		std::vector<float> err;
		if(prev_image_.empty())
			in_image.copyTo(prev_image_);
		cv::calcOpticalFlowPyrLK(prev_image_, 			//previous image frame
								gray_image, 			//current image frame
								prev_points_, 			//previous corner points
								current_points_, 		//current corner points (tracked)
								status,
								err,
								window_size_,
								3,
								term_criteria_,
								0,
								0.001);
		std::size_t i = 0, k = 0;

		current_centroid_x_ = 0;
		current_centroid_y_ = 0;

		//process points
		for (i=0, k=0 ; i < prev_points_.size(); i++)
		{
			if( !status[i] )
			{
				continue;
			}
			cv::Point2f p,q;
			p.x = (int)prev_points_[i].x;		p.y = (int)prev_points_[i].y;
			q.x = (int)current_points_[i].x;	q.y = (int)current_points_[i].y;

			sum_y = p.y-q.y;
			sum_x = p.x -q.x;

			current_centroid_x_+= current_points_[i].x;
			current_centroid_y_+= current_points_[i].y;

			current_points_[k++] = current_points_[i];
			cv::circle(in_image, current_points_[i], 3 , cv::Scalar(0,255,0), 2);
		}

		frame_count_++;
	}
	if (current_points_.size()<=0)
		return in_image;

	cv::Rect obj_rect;
	cv::Point centroid(current_centroid_x_/current_points_.size(), current_centroid_y_/current_points_.size());

	int valid_points = GetRectFromPoints(current_points_,
						centroid,
						matched_detection_,
						obj_rect);

	if (obj_rect.width <= matched_detection_.width*0.1 ||
			obj_rect.height <= matched_detection_.height*0.1)
	{
		std::cout << "TRACK STOPPED" << std::endl;
		prev_points_.clear();
		current_points_.clear();
		return in_image;
	}

	cv::rectangle(in_image, obj_rect, cv::Scalar(0,0,255), 2);

	if (prev_points_.size() > 0 )
	{
		cv::Point center_point = cv::Point(obj_rect.x + obj_rect.width/2, obj_rect.y + obj_rect.height/2);
		cv::Point direction_point;
		float sum_angle = atan2(sum_y, sum_x);

		direction_point.x = (center_point.x - 100 * cos(sum_angle));
		direction_point.y = (center_point.y - 100 * sin(sum_angle));

		ArrowedLine(in_image, center_point, direction_point, cv::Scalar(0,0,255), 2);

		cv::circle(in_image, center_point, 4 , cv::Scalar(0,0,255), 2);
		cv::circle(in_image, center_point, 2 , cv::Scalar(0,0,255), 2);
	}

	//imshow("KLT debug", in_image);
	//cvWaitKey(1);
	//finally store current state into previous
	std::swap(current_points_, prev_points_);
	cv::swap(prev_image_, gray_image);

	if (current_centroid_x_ > 0 && current_centroid_y_ > 0)
	{
		previous_centroid_x_ = current_centroid_x_;
		previous_centroid_y_ = current_centroid_y_;
	}

	timer.stop();

	std::cout << timer.getTimeMilli() << std::endl;

	return in_image;
}

int LkTracker::GetRectFromPoints(std::vector< cv::Point2f > in_corners_points, cv::Point in_centroid, cv::Rect in_initial_box, cv::Rect& out_boundingbox)
{
	int num_points = 0;
	if (in_corners_points.empty())
		return num_points;

	int min_x=in_corners_points[0].x, min_y=in_corners_points[0].y, max_x=in_corners_points[0].x, max_y=in_corners_points[0].y;

	float top_x = in_centroid.x + in_initial_box.width/2;
	float low_x = in_centroid.x - in_initial_box.width/2;
	float top_y = in_centroid.y + in_initial_box.height/2;
	float low_y = in_centroid.y - in_initial_box.height/2;

	for (unsigned int i=0; i<in_corners_points.size(); i++)
	{
		bool sum_point = true;
		if (in_corners_points[i].x > 0 &&
				(in_corners_points[i].x <= top_x) &&
				(in_corners_points[i].x >= low_x) &&
				(in_corners_points[i].y <= top_y) &&
				(in_corners_points[i].y >= low_y)
			)
		{
			if (in_corners_points[i].x < min_x)
				min_x = in_corners_points[i].x;
			if (in_corners_points[i].x > max_x)
				max_x = in_corners_points[i].x;
		}
		else
			sum_point = false;

		if (in_corners_points[i].y > 0 &&
				(in_corners_points[i].x <= top_x) &&
				(in_corners_points[i].x >= low_x) &&
				(in_corners_points[i].y <= top_y) &&
				(in_corners_points[i].y >= low_y)
			)
		{
			if (in_corners_points[i].y < min_y)
				min_y = in_corners_points[i].y;
			if (in_corners_points[i].y > max_y)
				max_y = in_corners_points[i].y;
		}
		else
			sum_point = false;

		if (sum_point)
			num_points++;
	}
	out_boundingbox.x 		= min_x;
	out_boundingbox.y 		= min_y;
	out_boundingbox.width 	= max_x - min_x;
	if (out_boundingbox.width > in_initial_box.width)
		out_boundingbox.width = in_initial_box.width;
	out_boundingbox.height 	= max_y - min_y;
	if (out_boundingbox.height > in_initial_box.height)
				out_boundingbox.height = in_initial_box.height;

	return num_points;
}

